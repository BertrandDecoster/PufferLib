"""D4 symmetry utilities and equivariant neural networks.

This module provides:
1. Data augmentation functions for D4 symmetry (apply_d4_transform, random_d4_augment)
2. D4-equivariant neural networks using escnn (D4EquivariantEncoder, D4Actor, D4Critic)

D4 is the dihedral group of order 8 - the symmetry group of a square.
It consists of 4 rotations (0, 90, 180, 270 degrees CCW) and 4 reflections.
"""

import torch
import torch.nn as nn

import pufferlib.pytorch

try:
    from escnn import gspaces
    from escnn import nn as enn
    ESCNN_AVAILABLE = True
except ImportError:
    ESCNN_AVAILABLE = False
    gspaces = None
    enn = None


# =============================================================================
# D4 Data Augmentation: Exploit symmetry without escnn
# =============================================================================


def apply_d4_transform(
    obs: torch.Tensor, action: torch.Tensor | None = None, transform_idx: int = 0
) -> tuple[torch.Tensor, torch.Tensor | None]:
    """Apply a D4 transformation to observation and action.

    D4 has 8 elements: 4 rotations × 2 (identity or reflection)
    - 0: identity
    - 1: rotate 90° CCW
    - 2: rotate 180°
    - 3: rotate 270° CCW
    - 4: horizontal flip
    - 5: horizontal flip + rotate 90° CCW
    - 6: horizontal flip + rotate 180°
    - 7: horizontal flip + rotate 270° CCW

    Args:
        obs: Observation tensor [..., C, H, W]
        action: Optional action tensor with movement indices
        transform_idx: Which D4 element to apply (0-7)

    Returns:
        Transformed observation and action
    """
    # Movement action permutations under D4
    # Actions: [Stay=0, Up=1, Down=2, Left=3, Right=4]
    # Under 90° CCW rotation: Up→Left, Left→Down, Down→Right, Right→Up
    ACTION_PERMS = {
        0: [0, 1, 2, 3, 4],  # identity
        1: [0, 3, 4, 2, 1],  # 90° CCW: Up→Left, Down→Right, Left→Down, Right→Up
        2: [0, 2, 1, 4, 3],  # 180°: Up→Down, Down→Up, Left→Right, Right→Left
        3: [0, 4, 3, 1, 2],  # 270° CCW: Up→Right, Down→Left, Left→Up, Right→Down
        4: [0, 1, 2, 4, 3],  # H-flip: Left↔Right
        5: [0, 4, 3, 2, 1],  # H-flip + 90° CCW
        6: [0, 2, 1, 3, 4],  # H-flip + 180°
        7: [0, 3, 4, 1, 2],  # H-flip + 270° CCW
    }

    # Apply rotation/flip to observation
    k = transform_idx % 4  # Number of 90° rotations
    flip = transform_idx >= 4

    transformed_obs = obs
    if flip:
        transformed_obs = torch.flip(transformed_obs, dims=[-1])  # Horizontal flip
    if k > 0:
        transformed_obs = torch.rot90(transformed_obs, k=k, dims=[-2, -1])

    # Transform action if provided
    transformed_action = None
    if action is not None:
        perm = ACTION_PERMS[transform_idx]
        perm_tensor = torch.tensor(perm, device=action.device, dtype=action.dtype)
        # action contains movement indices (0-4), map through permutation
        transformed_action = perm_tensor[action.long()]

    return transformed_obs, transformed_action


def random_d4_augment(
    obs: torch.Tensor, action: torch.Tensor | None = None
) -> tuple[torch.Tensor, torch.Tensor | None, int]:
    """Apply a random D4 transformation for data augmentation.

    Args:
        obs: Observation tensor [..., C, H, W]
        action: Optional action tensor with movement indices

    Returns:
        Transformed observation, transformed action, and the transform index used
    """
    transform_idx = torch.randint(0, 8, (1,)).item()
    transformed_obs, transformed_action = apply_d4_transform(obs, action, transform_idx)
    return transformed_obs, transformed_action, transform_idx


def inverse_d4_transform(transform_idx: int) -> int:
    """Get the inverse D4 transformation index.

    Our apply_d4_transform applies: flip first (if idx >= 4), then rotate k times.
    Let T_k = rot_k, and F = horizontal flip.

    For pure rotations (0-3):
        T_k^{-1} = T_{4-k mod 4}

    For flip+rotate (4-7):
        The combined transform is T_k ∘ F (flip first, then rotate).
        Applying it twice: T_k ∘ F ∘ T_k ∘ F
        Since F flips horizontally and T_k rotates, we get:
        F ∘ T_k = T_{-k} ∘ F  (flip conjugates rotation to its inverse)
        So: T_k ∘ F ∘ T_k ∘ F = T_k ∘ T_{-k} ∘ F ∘ F = I
        Therefore flip+rot_k is self-inverse!

    Args:
        transform_idx: Original transform index (0-7)

    Returns:
        Index of the inverse transformation
    """
    # Verified empirically: flip+rotate transforms are self-inverse
    INVERSES = {
        0: 0,  # identity
        1: 3,  # rot_1 → rot_3
        2: 2,  # rot_2 → rot_2 (180° is self-inverse)
        3: 1,  # rot_3 → rot_1
        4: 4,  # flip → flip (self-inverse)
        5: 5,  # flip+rot_1 → self-inverse
        6: 6,  # flip+rot_2 → self-inverse
        7: 7,  # flip+rot_3 → self-inverse
    }
    return INVERSES[transform_idx]


# =============================================================================
# D4-Equivariant Networks using escnn
# =============================================================================


class D4EquivariantEncoder(nn.Module):
    """D4-equivariant CNN encoder using escnn.

    This encoder is equivariant to the D4 group (4 rotations × 2 reflections).
    For a square grid game with D4 symmetry, this provides:
    - 8× effective data (one orientation gives all 8 for free)
    - Better generalization to unseen rotations/reflections
    - 2-5× sample efficiency improvement

    The encoder outputs D4-invariant features (via GroupPooling), making it
    suitable for critic networks. For actor networks, use D4Actor which
    handles action equivariance.

    Args:
        in_channels: Number of input channels (observation planes)
        hidden_dim: Output feature dimension
        n_hidden: Number of hidden representations (channels = n_hidden * 8 for D4)
    """

    def __init__(
        self,
        in_channels: int = 7,
        hidden_dim: int = 64,
        n_hidden: int = 8,
    ):
        if not ESCNN_AVAILABLE:
            raise ImportError(
                "escnn is required for D4EquivariantEncoder. "
                "Install with: pip install escnn"
            )

        super().__init__()
        self.hidden_dim = hidden_dim

        # D4 group: 4 rotations × 2 reflections = 8 elements
        self.gspace = gspaces.flipRot2dOnR2(N=4)

        # Field types
        # Input: scalar fields (trivial representation, no transformation)
        self.in_type = enn.FieldType(
            self.gspace, in_channels * [self.gspace.trivial_repr]
        )

        # Hidden: regular representation (transforms under D4)
        # Each regular repr has 8 channels (one per group element)
        self.hid_type = enn.FieldType(
            self.gspace, n_hidden * [self.gspace.regular_repr]
        )  # n_hidden * 8 channels

        # Build equivariant network
        self.conv = enn.SequentialModule(
            # Stem
            enn.R2Conv(self.in_type, self.hid_type, kernel_size=3, padding=1),
            enn.InnerBatchNorm(self.hid_type),
            enn.ReLU(self.hid_type, inplace=False),
            # Block 1
            enn.R2Conv(self.hid_type, self.hid_type, kernel_size=3, padding=1),
            enn.InnerBatchNorm(self.hid_type),
            enn.ReLU(self.hid_type, inplace=False),
            # Block 2
            enn.R2Conv(self.hid_type, self.hid_type, kernel_size=3, padding=1),
            enn.InnerBatchNorm(self.hid_type),
            enn.ReLU(self.hid_type, inplace=False),
            # Spatial pooling
            enn.PointwiseAdaptiveAvgPool2D(self.hid_type, output_size=1),
            # Group pooling: regular -> trivial (D4 invariant)
            enn.GroupPooling(self.hid_type),
        )

        # Output type after GroupPooling: n_hidden trivial representations
        out_channels = n_hidden

        # Final linear projection to hidden_dim
        self.fc = nn.Sequential(
            nn.Flatten(),
            pufferlib.pytorch.layer_init(nn.Linear(out_channels, hidden_dim)),
            nn.ReLU(),
        )

    def forward(self, x: torch.Tensor) -> torch.Tensor:
        """Forward pass.

        Args:
            x: Tensor of shape [..., C, H, W]

        Returns:
            D4-invariant features of shape [..., hidden_dim]
        """
        # Handle arbitrary batch dimensions
        orig_shape = x.shape[:-3]
        x = x.reshape(-1, *x.shape[-3:])  # [batch, C, H, W]

        # Wrap in GeometricTensor
        x = enn.GeometricTensor(x, self.in_type)

        # Equivariant convolutions + pooling
        x = self.conv(x)

        # Extract tensor and project to hidden_dim
        x = self.fc(x.tensor)

        return x.reshape(*orig_shape, -1)  # [..., hidden_dim]


class D4Actor(nn.Module):
    """D4-equivariant actor for multi-agent settings.

    This actor uses a D4-equivariant encoder with EQUIVARIANT action heads
    for movement actions. The key insight: if we rotate the observation 90° CCW,
    the optimal movement action also rotates (Up → Left).

    Architecture:
    - Encoder: D4-equivariant CNN with spatial pooling (keeps equivariant features)
    - Movement head: Equivariant linear layer outputting:
        - 1D trivial (Stay logit - invariant)
        - 2D irrep_1,1 (direction vector that rotates with input)
      The 2D direction vector is projected to 4 directional logits (Up/Down/Left/Right)
    - Interaction head: GroupPooling → regular linear (invariant output)

    The movement actions [Stay, Up, Down, Left, Right] are handled as:
    - Stay: D4-invariant (same logit regardless of rotation)
    - Directions: D4-equivariant via 2D irrep, then projected to 4 logits

    Args:
        n_agents: Number of agents
        nvec: List of action dimensions [n_movement, n_interaction] = [5, 2]
        in_channels: Number of input channels
        hidden_dim: Hidden layer dimension
        n_hidden: Number of hidden representations for encoder
        vector_size: Size of vector observation (0 if not used)
    """

    def __init__(
        self,
        n_agents: int,
        nvec: list[int],
        in_channels: int = 7,
        hidden_dim: int = 64,
        n_hidden: int = 8,
        vector_size: int = 0,
    ):
        if not ESCNN_AVAILABLE:
            raise ImportError(
                "escnn is required for D4Actor. Install with: pip install escnn"
            )

        super().__init__()
        self.n_agents = n_agents
        self.nvec = nvec
        self.hidden_dim = hidden_dim
        self.n_hidden = n_hidden
        self.vector_size = vector_size

        # D4 group for 2D space (convolutions)
        self.gspace = gspaces.flipRot2dOnR2(N=4)
        self.G = self.gspace.fibergroup

        # Field types for CNN
        self.in_type = enn.FieldType(
            self.gspace, in_channels * [self.gspace.trivial_repr]
        )
        self.hid_type = enn.FieldType(
            self.gspace, n_hidden * [self.gspace.regular_repr]
        )

        # Equivariant encoder WITHOUT GroupPooling (keeps equivariant features)
        self.encoder = enn.SequentialModule(
            enn.R2Conv(self.in_type, self.hid_type, kernel_size=3, padding=1),
            enn.InnerBatchNorm(self.hid_type),
            enn.ReLU(self.hid_type, inplace=False),
            enn.R2Conv(self.hid_type, self.hid_type, kernel_size=3, padding=1),
            enn.InnerBatchNorm(self.hid_type),
            enn.ReLU(self.hid_type, inplace=False),
            enn.R2Conv(self.hid_type, self.hid_type, kernel_size=3, padding=1),
            enn.InnerBatchNorm(self.hid_type),
            enn.ReLU(self.hid_type, inplace=False),
            enn.PointwiseAdaptiveAvgPool2D(self.hid_type, output_size=1),
            # NO GroupPooling here - we keep equivariant features
        )

        # For MLP layers, use no_base_space GSpace
        from escnn import group
        self.mlp_gspace = gspaces.no_base_space(group.dihedral_group(4))

        # Hidden type for MLP (must match encoder output structure)
        # After PointwiseAdaptiveAvgPool2D with output_size=1, we have [B, n_hidden*8, 1, 1]
        # The regular representation has 8 channels per field
        self.mlp_hid_type = self.mlp_gspace.type(
            *[self.mlp_gspace.regular_repr for _ in range(n_hidden)]
        )

        # Movement output type: 1 trivial (Stay) + 1 irrep_1,1 (2D direction)
        # Total: 3 dimensions that transform equivariantly
        irrep_2d = self.mlp_gspace.fibergroup.irrep(1, 1)
        self.movement_out_type = self.mlp_gspace.type(
            self.mlp_gspace.trivial_repr,  # Stay logit (invariant)
            irrep_2d,  # 2D direction vector (equivariant)
        )

        # Equivariant linear for movement
        self.movement_linear = enn.Linear(self.mlp_hid_type, self.movement_out_type)

        # Direction vectors for projecting 2D equivariant output to 4 directional logits
        # escnn element 3 (CW rotation matching env d4_transform=1) transforms (x,y) -> (y,-x)
        # Under this transform, we need: Up->Right, Right->Down, Down->Left, Left->Up
        # This encoding achieves that:
        self.register_buffer('direction_vectors', torch.tensor([
            [1.0, 0.0],   # Up
            [-1.0, 0.0],  # Down
            [0.0, 1.0],   # Left (swapped with Right)
            [0.0, -1.0],  # Right (swapped with Left)
        ]))  # [4, 2]

        # Interaction head: invariant output via GroupPooling
        self.group_pool = enn.GroupPooling(self.hid_type)

        # Vector MLP (for D4-invariant vector features like steps_left)
        if vector_size > 0:
            self.vector_mlp = nn.Sequential(
                pufferlib.pytorch.layer_init(nn.Linear(vector_size, hidden_dim)),
                nn.ReLU(),
            )
            # Interaction head: GroupPooled features (n_hidden) + vector MLP output (hidden_dim)
            self.interaction_head = nn.Sequential(
                pufferlib.pytorch.layer_init(nn.Linear(n_hidden + hidden_dim, hidden_dim)),
                nn.ReLU(),
                pufferlib.pytorch.layer_init(nn.Linear(hidden_dim, nvec[1]), std=0.01),
            )
        else:
            self.vector_mlp = None
            # Interaction head: only GroupPooled features (n_hidden channels)
            self.interaction_head = nn.Sequential(
                nn.Flatten(),
                pufferlib.pytorch.layer_init(nn.Linear(n_hidden, hidden_dim)),
                nn.ReLU(),
                pufferlib.pytorch.layer_init(nn.Linear(hidden_dim, nvec[1]), std=0.01),
            )

    def forward(self, obs: torch.Tensor, vector: torch.Tensor = None) -> tuple[torch.Tensor, ...]:
        """Forward pass.

        Args:
            obs: Observation tensor of shape [..., n_agents, C, H, W]
            vector: Optional vector observation of shape [..., vector_size]

        Returns:
            Tuple of (movement_logits, interaction_logits).
            movement_logits: [..., n_agents, 5] for [Stay, Up, Down, Left, Right]
            interaction_logits: [..., n_agents, 2] for [Noop, Attack]
        """
        orig_shape = obs.shape[:-3]
        obs_flat = obs.reshape(-1, *obs.shape[-3:])
        batch_size = obs_flat.shape[0]

        # Equivariant encoding (keeps equivariant structure)
        geom_obs = enn.GeometricTensor(obs_flat, self.in_type)
        features = self.encoder(geom_obs)  # GeometricTensor [B, n_hidden*8, 1, 1]

        # --- Movement head (equivariant) ---
        # Reshape for MLP: [B, n_hidden*8, 1, 1] -> [B, n_hidden*8]
        features_flat = features.tensor.squeeze(-1).squeeze(-1)  # [B, n_hidden*8]
        # Wrap in GeometricTensor for MLP gspace
        mlp_features = enn.GeometricTensor(features_flat, self.mlp_hid_type)
        # Equivariant linear
        movement_out = self.movement_linear(mlp_features).tensor  # [B, 3]

        # Split: Stay (1D) + Direction (2D)
        stay_logit = movement_out[:, :1]  # [B, 1]
        direction_vec = movement_out[:, 1:]  # [B, 2]

        # Project direction vector to 4 directional logits
        # logit_i = direction_vec · direction_vectors[i]
        direction_logits = torch.matmul(
            direction_vec, self.direction_vectors.T
        )  # [B, 4]

        # Combine: [Stay, Up, Down, Left, Right]
        movement_logits = torch.cat([stay_logit, direction_logits], dim=-1)  # [B, 5]
        movement_logits = movement_logits.reshape(*orig_shape, 5)

        # --- Interaction head (invariant) ---
        # Apply GroupPooling to make features invariant
        invariant_features = self.group_pool(features).tensor  # [B, n_hidden, 1, 1]
        invariant_flat = invariant_features.flatten(1)  # [B, n_hidden]

        # Concatenate with vector features if available
        if self.vector_size > 0 and vector is not None:
            vector_flat = vector.reshape(-1, self.vector_size)  # [B, vector_size]
            vector_features = self.vector_mlp(vector_flat)  # [B, hidden_dim]
            combined = torch.cat([invariant_flat, vector_features], dim=1)
            interaction_logits = self.interaction_head(combined)  # [B, 2]
        else:
            interaction_logits = self.interaction_head(invariant_features)  # [B, 2]

        interaction_logits = interaction_logits.reshape(*orig_shape, 2)

        return (movement_logits, interaction_logits)


class D4Critic(nn.Module):
    """D4-equivariant critic for multi-agent settings.

    Uses D4-equivariant encoder and outputs D4-invariant values.
    The state value should be the same regardless of orientation.

    Args:
        n_agents: Number of agents
        in_channels: Number of input channels
        hidden_dim: Hidden layer dimension
        centralised: If True, use centralized critic (MAPPO style)
        n_hidden: Number of hidden representations for encoder
        vector_size: Size of vector observation (0 if not used)
    """

    def __init__(
        self,
        n_agents: int,
        in_channels: int = 7,
        hidden_dim: int = 64,
        centralised: bool = False,
        n_hidden: int = 8,
        vector_size: int = 0,
    ):
        if not ESCNN_AVAILABLE:
            raise ImportError(
                "escnn is required for D4Critic. Install with: pip install escnn"
            )

        super().__init__()
        self.n_agents = n_agents
        self.centralised = centralised
        self.vector_size = vector_size
        self.hidden_dim = hidden_dim

        # Use D4-equivariant encoder (outputs invariant features)
        self.encoder = D4EquivariantEncoder(in_channels, hidden_dim, n_hidden)

        # Vector MLP (for D4-invariant vector features like steps_left)
        if vector_size > 0:
            self.vector_mlp = nn.Sequential(
                pufferlib.pytorch.layer_init(nn.Linear(vector_size, hidden_dim)),
                nn.ReLU(),
            )
            # Value head: encoder output (hidden_dim) + vector MLP output (hidden_dim)
            if centralised:
                self.head = pufferlib.pytorch.layer_init(
                    nn.Linear((hidden_dim + hidden_dim) * n_agents, 1), std=1.0
                )
            else:
                self.head = pufferlib.pytorch.layer_init(
                    nn.Linear(hidden_dim + hidden_dim, 1), std=1.0
                )
        else:
            self.vector_mlp = None
            if centralised:
                self.head = pufferlib.pytorch.layer_init(
                    nn.Linear(hidden_dim * n_agents, 1), std=1.0
                )
            else:
                self.head = pufferlib.pytorch.layer_init(nn.Linear(hidden_dim, 1), std=1.0)

    def forward(self, obs: torch.Tensor, vector: torch.Tensor = None) -> torch.Tensor:
        """Forward pass.

        Args:
            obs: Observation tensor of shape [..., n_agents, C, H, W]
            vector: Optional vector observation of shape [..., vector_size]

        Returns:
            State values of shape [..., n_agents, 1]
        """
        batch_shape = obs.shape[:-4]
        n_agents_dim = obs.shape[-4]

        if self.centralised:
            obs_flat = obs.reshape(-1, *obs.shape[-3:])
            features = self.encoder(obs_flat)

            # Concatenate with vector features if available
            if self.vector_size > 0 and vector is not None:
                vector_flat = vector.reshape(-1, self.vector_size)
                vector_features = self.vector_mlp(vector_flat)
                features = torch.cat([features, vector_features], dim=1)

            features = features.reshape(*batch_shape, -1)
            value = self.head(features)
            return value.unsqueeze(-2).expand(*batch_shape, self.n_agents, 1)
        else:
            obs_flat = obs.reshape(-1, *obs.shape[-3:])
            features = self.encoder(obs_flat)

            # Concatenate with vector features if available
            if self.vector_size > 0 and vector is not None:
                vector_flat = vector.reshape(-1, self.vector_size)
                vector_features = self.vector_mlp(vector_flat)
                features = torch.cat([features, vector_features], dim=1)

            values = self.head(features)
            return values.reshape(*batch_shape, n_agents_dim, 1)


# =============================================================================
# D4-Equivariant Networks V2: Matching SynchroMobaTemplate structure
# =============================================================================


class D4ActorV2(nn.Module):
    """D4-equivariant actor matching SynchroMobaTemplate structure.

    Key differences from D4Actor:
    - n_hidden=16 by default (16 × 8 = 128 channels, matching SynchroMobaTemplate)
    - stride=2 in first R2Conv (spatial downsampling like SynchroMobaTemplate)
    - 2 R2Conv layers (matching SynchroMobaTemplate's 2 Conv2d layers)
    - Equivariant movement head (actions transform with input rotation)

    Args:
        n_agents: Number of agents
        nvec: List of action dimensions [n_movement, n_interaction] = [5, 2]
        in_channels: Number of input channels
        cnn_channels: Total CNN channels (must be divisible by 8 for D4 regular repr)
        hidden_size: Hidden layer dimension for fusion/heads
        vector_size: Size of vector observation (0 if not used)
    """

    def __init__(
        self,
        n_agents: int,
        nvec: list[int],
        in_channels: int = 7,
        cnn_channels: int = 128,
        hidden_size: int = 128,
        vector_size: int = 0,
    ):
        if not ESCNN_AVAILABLE:
            raise ImportError(
                "escnn is required for D4ActorV2. Install with: pip install escnn"
            )

        super().__init__()
        self.n_agents = n_agents
        self.nvec = nvec
        self.hidden_size = hidden_size
        self.vector_size = vector_size

        # n_hidden = number of regular representation fields
        # Each field has 8 channels (one per D4 group element)
        n_hidden = cnn_channels // 8
        self.n_hidden = n_hidden

        # D4 group for 2D space (convolutions)
        self.gspace = gspaces.flipRot2dOnR2(N=4)
        self.G = self.gspace.fibergroup

        # Field types for CNN
        self.in_type = enn.FieldType(
            self.gspace, in_channels * [self.gspace.trivial_repr]
        )
        self.hid_type = enn.FieldType(
            self.gspace, n_hidden * [self.gspace.regular_repr]
        )

        # Equivariant encoder - matching SynchroMobaTemplate structure
        # Conv2d(5, 128, 3, stride=2) -> R2Conv(5 trivial, 16 regular, 3, stride=2)
        # Conv2d(128, 128, 3, stride=1) -> R2Conv(16 regular, 16 regular, 3, stride=1)
        self.encoder = enn.SequentialModule(
            enn.R2Conv(self.in_type, self.hid_type, kernel_size=3, stride=2, padding=1),
            enn.ReLU(self.hid_type, inplace=False),
            enn.R2Conv(self.hid_type, self.hid_type, kernel_size=3, stride=1, padding=1),
            enn.ReLU(self.hid_type, inplace=False),
            enn.PointwiseAdaptiveAvgPool2D(self.hid_type, output_size=1),
            # NO GroupPooling here - we keep equivariant features for movement head
        )

        # For MLP layers, use no_base_space GSpace
        from escnn import group
        self.mlp_gspace = gspaces.no_base_space(group.dihedral_group(4))

        # Hidden type for MLP (must match encoder output structure)
        self.mlp_hid_type = self.mlp_gspace.type(
            *[self.mlp_gspace.regular_repr for _ in range(n_hidden)]
        )

        # Movement output type: 1 trivial (Stay) + 1 irrep_1,1 (2D direction)
        irrep_2d = self.mlp_gspace.fibergroup.irrep(1, 1)
        self.movement_out_type = self.mlp_gspace.type(
            self.mlp_gspace.trivial_repr,  # Stay logit (invariant)
            irrep_2d,  # 2D direction vector (equivariant)
        )

        # Equivariant linear for movement
        self.movement_linear = enn.Linear(self.mlp_hid_type, self.movement_out_type)

        # Direction vectors for projecting 2D equivariant output to 4 directional logits
        self.register_buffer('direction_vectors', torch.tensor([
            [1.0, 0.0],   # Up
            [-1.0, 0.0],  # Down
            [0.0, 1.0],   # Left
            [0.0, -1.0],  # Right
        ]))

        # Interaction head: invariant output via GroupPooling
        self.group_pool = enn.GroupPooling(self.hid_type)

        # Vector MLP (for D4-invariant vector features)
        if vector_size > 0:
            self.vector_mlp = nn.Sequential(
                pufferlib.pytorch.layer_init(nn.Linear(vector_size, hidden_size)),
                nn.ReLU(),
            )
            # Interaction head: GroupPooled features (n_hidden) + vector MLP (hidden_size)
            self.interaction_head = nn.Sequential(
                pufferlib.pytorch.layer_init(nn.Linear(n_hidden + hidden_size, hidden_size)),
                nn.ReLU(),
                pufferlib.pytorch.layer_init(nn.Linear(hidden_size, nvec[1]), std=0.01),
            )
        else:
            self.vector_mlp = None
            self.interaction_head = nn.Sequential(
                pufferlib.pytorch.layer_init(nn.Linear(n_hidden, hidden_size)),
                nn.ReLU(),
                pufferlib.pytorch.layer_init(nn.Linear(hidden_size, nvec[1]), std=0.01),
            )

    def forward(self, obs: torch.Tensor, vector: torch.Tensor = None) -> tuple[torch.Tensor, ...]:
        """Forward pass.

        Args:
            obs: Observation tensor of shape [..., n_agents, C, H, W]
            vector: Optional vector observation of shape [..., vector_size]

        Returns:
            Tuple of (movement_logits, interaction_logits).
        """
        orig_shape = obs.shape[:-3]
        obs_flat = obs.reshape(-1, *obs.shape[-3:])
        batch_size = obs_flat.shape[0]

        # Equivariant encoding
        geom_obs = enn.GeometricTensor(obs_flat, self.in_type)
        features = self.encoder(geom_obs)  # GeometricTensor [B, n_hidden*8, 1, 1]

        # --- Movement head (equivariant) ---
        features_flat = features.tensor.squeeze(-1).squeeze(-1)  # [B, n_hidden*8]
        mlp_features = enn.GeometricTensor(features_flat, self.mlp_hid_type)
        movement_out = self.movement_linear(mlp_features).tensor  # [B, 3]

        # Split: Stay (1D) + Direction (2D)
        stay_logit = movement_out[:, :1]  # [B, 1]
        direction_vec = movement_out[:, 1:]  # [B, 2]

        # Project direction vector to 4 directional logits
        direction_logits = torch.matmul(direction_vec, self.direction_vectors.T)  # [B, 4]

        # Combine: [Stay, Up, Down, Left, Right]
        movement_logits = torch.cat([stay_logit, direction_logits], dim=-1)  # [B, 5]
        movement_logits = movement_logits.reshape(*orig_shape, 5)

        # --- Interaction head (invariant) ---
        invariant_features = self.group_pool(features).tensor  # [B, n_hidden, 1, 1]
        invariant_flat = invariant_features.flatten(1)  # [B, n_hidden]

        if self.vector_size > 0 and vector is not None:
            vector_flat = vector.reshape(-1, self.vector_size)
            vector_features = self.vector_mlp(vector_flat)
            combined = torch.cat([invariant_flat, vector_features], dim=1)
            interaction_logits = self.interaction_head(combined)
        else:
            interaction_logits = self.interaction_head(invariant_flat)

        interaction_logits = interaction_logits.reshape(*orig_shape, 2)

        return (movement_logits, interaction_logits)


class D4CriticV2(nn.Module):
    """D4-equivariant critic matching SynchroMobaTemplate structure.

    Uses D4-equivariant encoder and outputs D4-invariant values.
    The state value should be the same regardless of orientation.

    Args:
        n_agents: Number of agents
        in_channels: Number of input channels
        cnn_channels: Total CNN channels (must be divisible by 8)
        hidden_size: Hidden layer dimension
        centralised: If True, use centralized critic (MAPPO style)
        vector_size: Size of vector observation (0 if not used)
    """

    def __init__(
        self,
        n_agents: int,
        in_channels: int = 7,
        cnn_channels: int = 128,
        hidden_size: int = 128,
        centralised: bool = False,
        vector_size: int = 0,
    ):
        if not ESCNN_AVAILABLE:
            raise ImportError(
                "escnn is required for D4CriticV2. Install with: pip install escnn"
            )

        super().__init__()
        self.n_agents = n_agents
        self.centralised = centralised
        self.vector_size = vector_size
        self.hidden_size = hidden_size

        n_hidden = cnn_channels // 8
        self.n_hidden = n_hidden

        # D4 group
        self.gspace = gspaces.flipRot2dOnR2(N=4)

        # Field types
        self.in_type = enn.FieldType(
            self.gspace, in_channels * [self.gspace.trivial_repr]
        )
        self.hid_type = enn.FieldType(
            self.gspace, n_hidden * [self.gspace.regular_repr]
        )

        # Equivariant encoder with GroupPooling for invariant output
        self.encoder = enn.SequentialModule(
            enn.R2Conv(self.in_type, self.hid_type, kernel_size=3, stride=2, padding=1),
            enn.ReLU(self.hid_type, inplace=False),
            enn.R2Conv(self.hid_type, self.hid_type, kernel_size=3, stride=1, padding=1),
            enn.ReLU(self.hid_type, inplace=False),
            enn.PointwiseAdaptiveAvgPool2D(self.hid_type, output_size=1),
            enn.GroupPooling(self.hid_type),  # D4-invariant output
        )

        # Vector MLP
        if vector_size > 0:
            self.vector_mlp = nn.Sequential(
                pufferlib.pytorch.layer_init(nn.Linear(vector_size, hidden_size)),
                nn.ReLU(),
            )
            # Value head: encoder output (n_hidden) + vector MLP (hidden_size)
            feature_size = n_hidden + hidden_size
            if centralised:
                self.head = pufferlib.pytorch.layer_init(
                    nn.Linear(feature_size * n_agents, 1), std=1.0
                )
            else:
                self.head = nn.Sequential(
                    pufferlib.pytorch.layer_init(nn.Linear(feature_size, hidden_size)),
                    nn.ReLU(),
                    pufferlib.pytorch.layer_init(nn.Linear(hidden_size, 1), std=1.0),
                )
        else:
            self.vector_mlp = None
            if centralised:
                self.head = pufferlib.pytorch.layer_init(
                    nn.Linear(n_hidden * n_agents, 1), std=1.0
                )
            else:
                self.head = nn.Sequential(
                    pufferlib.pytorch.layer_init(nn.Linear(n_hidden, hidden_size)),
                    nn.ReLU(),
                    pufferlib.pytorch.layer_init(nn.Linear(hidden_size, 1), std=1.0),
                )

    def forward(self, obs: torch.Tensor, vector: torch.Tensor = None) -> torch.Tensor:
        """Forward pass.

        Args:
            obs: Observation tensor of shape [..., n_agents, C, H, W]
            vector: Optional vector observation of shape [..., vector_size]

        Returns:
            State values of shape [..., n_agents, 1]
        """
        batch_shape = obs.shape[:-4]
        n_agents_dim = obs.shape[-4]

        obs_flat = obs.reshape(-1, *obs.shape[-3:])

        # Equivariant encoding with GroupPooling → invariant
        geom_obs = enn.GeometricTensor(obs_flat, self.in_type)
        features = self.encoder(geom_obs).tensor  # [B, n_hidden, 1, 1]
        features = features.flatten(1)  # [B, n_hidden]

        # Concatenate with vector features if available
        if self.vector_size > 0 and vector is not None:
            vector_flat = vector.reshape(-1, self.vector_size)
            vector_features = self.vector_mlp(vector_flat)
            features = torch.cat([features, vector_features], dim=1)

        if self.centralised:
            features = features.reshape(*batch_shape, -1)
            value = self.head(features)
            return value.unsqueeze(-2).expand(*batch_shape, self.n_agents, 1)
        else:
            values = self.head(features)
            return values.reshape(*batch_shape, n_agents_dim, 1)
