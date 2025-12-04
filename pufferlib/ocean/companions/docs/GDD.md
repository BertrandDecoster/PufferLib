Elevator pitch

**The Companions is a single player game that delivers the cooperative gameplay fantasy often missing in multiplayer thanks to hybrid AI companions with a high level HTN planner and a low level reinforcement learning neural network actor**

Slightly longer pitch

Multiplayer games sell the fantasy of cooperation, but the reality is often a toxic environment where everyone wants to be the hero of their own story. Single-player games make you the hero but the companions you get offer little actual gameplay cooperation as you typically end up controlling most of your team in some ways.

The companions delivers the best of both worlds. It provides the deep, strategic teamwork of multiplayer without the toxicity, in a single-player experience where you are the undisputed star. In turn-based games, players usually control the whole party, making an AI partner less useful. But in a real-time setting, the need for intelligent, autonomous partners is critical. The companions is built on this idea: you aren't micromanaging puppets. You are a conductor leading a team of genuinely smart AI partners who help you execute your grand strategy.

To achieve this, we want to use Reinforcement Learning (RL) as we believe it's an underutilised tool that can lead to innovative gameplay. But RL, in its general form is often impractical for games due to major hurdles: it's computationally expensive for consumer machines, struggles to adapt to real world "noisy" problems, and is notoriously poor at long-term planning. Our approach strategically turns these weaknesses into strengths.

Controlled Environment: We don't try to solve the real world. We've built a synthetic, where we define the rules and play by RL's strengths. The game uses a fully observable grid based system with discrete movement because that's where current RL thrives. 

Hybrid Planning: We solve long-term planning by not using RL at all. A high-level Hierarchical Task Network (HTN) planner explores the explicit winning strategies we design, handling the "what to do". This is another strength of being a synthetic environment, we can specify all the ways an objective can be accomplished, we let the player the fun of figuring the best way.

Specialised Agents: When the HTN planner sets a task, we use small, efficient RL agents only for tasks they excel at: complex, low-level, reactive actions. A standard pathfinding algorithm is better for just moving from A to B. But our RL agents can handle moving from A to B while simultaneously dodging AoE attacks and baiting specific enemies, a dynamic, multi-objective task that scripted AI can't handle effectively.

# Game Design Document

**1. Executive Summary**

The Companions is a real-time, top-down single-player action-strategy game. The **player** controls a unique hero, leading two AI-driven **companions** through challenging encounters against various **NPCs** (enemies, allies, and neutral parties). Success hinges on orchestrating the team's abilities via a novel **"Attunement System,"** where powerful effects require coordinated actions: a **Primer** to initiate, potentially one or more **Catalyst** abilities to augment or bridge, and a **Detonator** to unleash the full effect.

The game's core innovation lies in its AI: a high-level HTN (Hierarchical Task Network) planner devises multiple strategic solutions to encounters. One plan is dynamically chosen based on tactical advantage, companion inclinations, and the player's ongoing actions. Atomic tasks within the chosen plan, especially complex maneuvers or precise ability executions, are handled by small, dedicated Reinforcement Learning (RL) networks, ensuring both intelligent execution and a learning curve for the AI system itself. The player can enter a "Tactical Focus" mode (pause/super slow-motion) to inspect proposed plans and even force a specific strategy.

**2. Core Pillars**

- **Strategic Orchestration:** The player is the conductor. Their actions and decisions guide the flow of strategy.
- **Mandatory Cooperation:** No single entity (player or companion) can overcome significant challenges alone. The Attunement System is central to this.
- **Intelligent & Adaptive Companions:** Companions are not script-followers but proactive contributors, guided by a sophisticated AI planner, with full awareness of the environment.
- **Multiple Solutions:** Challenges are designed to be solvable in several distinct ways, encouraging experimentation and replayability. This includes manipulating enemy NPC FSMs, interacting with friendly/neutral NPCs, and solving environmental puzzles.
- **Player Agency in Execution:** While the AI plans, the player is crucial for initiating, enabling, and finalizing key actions, ensuring they are always "in the loop."

**3. Core Gameplay Mechanics**

**3.1. The Adaptive Planning System (HTN + RL)**

- **HTN Planner:**
    - Takes game state (player, companions and NPCs states, environmental state) and current objective as input.
    - Generates 2-3 viable, high-level plans (sequences of abstract tasks like "Neutralize Enemy Leader," "Disable Magical Ward," "Create Diversion," "Exploit Enemy Vulnerability Window").
    - Each plan has different requirements, risks, and potential outcomes, including steps to manipulate enemy NPC FSMs (e.g., enraging a duelist by evading, or capitalizing on a post-attack vulnerable state).
- **Plan Selection Algorithm:**
    - **Feasibility/Ease:** How likely is success? Are resources available?
    - **Companion Inclination:** Latent "preferences" in companions (e.g., one might favor aggressive approaches, another tactical positioning) subtly weight plans. These are not explicit choices but biases.
    - **Player Momentum:** The player's current actions and positioning can make one plan more immediately actionable or relevant. The system tries to flow with, not against, the player.
- **Task Execution:**
    - Plans decompose into atomic tasks (e.g., "Move to Flanking Position," "Cast 'Glyph of Weakening' on Target X," "Execute 'Shattering Blow' on Petrified Enemy").
    - Simple tasks (e.g., using a skill) are directly coded.
    - Complex tasks (e.g., optimal positioning for an AoE, precise timing for an interrupt, navigating complex terrain under fire) are delegated to small, specialized RL agents.
    - RL agents are trained for interchangeability: an RL agent for "precise spell targeting" could be used by any character possessing a skill requiring it. Companions have full access to environmental information for these tasks.

**3.2. Attunement System (Multi-step Combos)**

- **Fundamental Mechanic:** Most powerful abilities are multi-part "Attunements."
    - **Primer:** Initiates a condition or effect (e.g., "Chill Touch" applies Frost).
    - **Catalyst (Optional):** An intermediate ability that can modify, extend, or bridge Primer effects (e.g., "Arcane Conduit" links a Frost effect to a nearby target, or "Amplifying Ward" strengthens the Frost). Multiple Catalysts can sometimes be chained.
    - **Detonator:** Triggers an amplified or altered effect based on the Primed and Catalyzed state (e.g., "Shatter" on a Frost-affected target causes an AoE ice explosion).
- **Player-Companion Interplay:**
    - Companions can execute Primers, Catalysts, or Detonators as part of the active HTN plan.
    - The Player *must* be involved in nearly every significant Attunement, either by providing one of a Primer/Catalyst/Detonator, or by issuing a quick contextual command during Tactical Focus for a companion to execute their part, thereby setting up or completing the player's action.
    - Example: Arcanist companion casts "Oil Slick" (Primer). Player casts "Ignite" (Detonator) on the slick, creating a large fire. The HTN plan might have been "Control Chokepoint," and this Attunement is a task within it.

**3.3. Player Character & Companions**

- **Player Character:** A highly versatile hero. All skills are swappable and can be learned/equipped.
- **Companions (2):** Drawn from distinct archetypes (e.g., Warden, Arcanist, Skirmisher, Healer).
    - Has one unswappable skill. Defines the archetype and the personality/inclination
    - All other skills are swappable and can be learned/equipped, identical to the player's skill pool. This ensures interchangeability for RL agent training and strategic flexibility.
    - Latent "inclinations" (e.g., aggressive, defensive, prefers ranged, prefers melee) affect plan preference and slight behavioral styling during task execution.

**3.4. Information Dynamics & Tactical Focus**

- **Complete View:** There is no fog of war; the tactical situation is fully visible.
- **Fragmented Perception:** The player does not directly perceive abstract enemy states (e.g., "Heavily Armored" "Magically Resistant") though there can be cues.
- **Companion Insight:** Companions *can* perceive these states. They communicate this critical information via UI callouts or voice lines.
    - Example: A Warden companion might call out, "Their hide is like stone, direct attacks will be difficult!" or "Watch its charge pattern!"
    - Example: An Arcanist companion might observe, "That sigil on the pillar pulses with fire magic!" or "It seems vulnerable to frost!"
    The player synthesizes this to form a complete picture, which also informs the HTN's game state understanding.
- **Tactical Focus Mode:**
    - Player can trigger a pause or super slow-motion state.
    - The UI displays the HTN's top 2-3 proposed plans, highlighting key targets, paths, or abilities involved.
    - Selecting a plan element can provide more detail on the sequence of tasks.
    - The player can choose to "lock in" or force one of the presented plans.
    - Only viable plans are shown, or if non-viable plans are displayed for learning, they are clearly marked with the reasons for their current inviability (e.g., "Requires Target to be Distracted - Target is Alert").

**3.5. World Interaction & Movement**

- **Continuous Movement:** Player and companions move freely in a 2D top-down space.
- **Grid:** An underlying logical grid governs ability ranges, AoE templates, cover detection, and some AI positioning heuristics.
- **Environmental Interactions:** Many Attunements can interact with the environment (e.g., electrifying water, shattering cover, growing magical plants on fertile ground). This includes environmental puzzles requiring no enemy NPCs, such as synchronized pressure plates or redirecting magical energy flows.
- **Friendly & Neutral NPCs:** Levels can feature non-hostile NPCs who may offer quests, information, or require assistance, with HTN plans generated for these interactions (e.g., "Protect Villagers," "Escort Emissary").

**3.6. Core Gameplay principles** 

- Always have at least 2 or 3 ways to defeat an enemy.
- The plans should not be too convoluted (too long, players won’t find them) neither too short (is “I cast fireball and everyone dies” a satisfying plan?)
- The companions should not be able to solo the map. It’s possible to guarantee it by having the plans need the human player participation
- Always adapt to what the player is doing. A genius plan has no value if the player will not take part in it.

**4. AI System Deep Dive**

**4.1. HTN Planner**

- **Domain Definition:** A rich library of operators (actions like "Move," "CastSpell," "UseItem") and methods (recipes for decomposing tasks like "BreachDefenses" -> "DisableTrap" + "WeakenGate" + "ChargeThrough"). Includes methods for manipulating enemy FSM states (e.g., "TauntEnemy" to draw aggro, "ExploitOpening" after an enemy's special attack).
- **Real-time Re-planning:** If a plan is failing or the situation changes drastically, the HTN can re-evaluate and propose alternative plans or modifications.
- **Explainable AI (Goal):** Through UI cues in Tactical Focus mode, the player should get a sense of the *intent* and key steps behind the current plan.

**4.2. Reinforcement Learning Agents**

- **Dedicated Micro-Networks:** Small, focused NNs for specific low-level tasks: Dynamic Positioning, Precision Spell/Attack Targeting, Cooperative Movement.
- **Input State:** Full environmental information (positions of all entities, terrain features, active effects), target data, current skill cooldowns, player commands/intent.
- **Reward Functions:** Crafted to encourage efficient, effective, and cooperative execution of their assigned task.
- **Interchangeability:** RL agents are trained on *skill execution primitives*. If multiple characters have "Aimed Shot," the same RL agent handles its execution.
- Agent style: the RL reward is weighted by secondary considerations like time spent or health lost. The weights for these are given in input, so you can force an agent to be reckless

**4.3. Enemy NPC AI (Finite State Machines)**

- Designed for readability and counter-play. Behaviors are predictable but challenging in groups or when combined with environmental hazards.
- FSM states include clear vulnerabilities or behavioral shifts that the HTN can identify and build plans around (e.g., an Ogre's "exhausted" state after a rampage, a Duelist's "enraged" state if their challenge is ignored).
- FSMs are used because they allow for fast RL training
- **Basic Enemy Archetypes (Examples):**
    - **Marauders (Goblins/Bandits):** Fast, numerous, tend to swarm. Individually weak but dangerous in groups. Vulnerable to area-of-effect Attunements.
    - **Guardian Constructs:** Heavily armored, slower, with powerful but telegraphed attacks. Often vulnerable to specific elemental damage types or Attunements that exploit momentary weaknesses after their attacks.
    - **Wild Elementals:** Immune to their own element, highly vulnerable to opposing elements. Their attacks often infuse the environment with their element, creating hazards or opportunities.
- **Enemy Attunement & Environmental Interactions:**
    - Certain enemies, when affected by specific Primers or environmental conditions, can become part of an Attunement chain or an environmental puzzle.
        - Example: A "Frost-Imbued Elemental," if shattered by a powerful Detonator, could leave behind a temporary "Ice Block" that could be used for cover or to block a passage.
        - Example: An enemy doused in "Conductive Oil" (a Primer) might cause any lightning-based Detonator to arc to nearby allies or conductive surfaces.
- **Boss Encounters:**
    - Multi-phase battles requiring the player and companions to adapt strategies and Attunement combinations.
    - Clear "vulnerability windows" often requiring specific Primer-Catalyst-Detonator chains to maximize damage or interrupt powerful abilities.
    - Some bosses may exhibit adaptive behaviors, becoming more resistant to repeatedly used Attunements, encouraging diverse tactical approaches.

**5. Character Archetypes & Skills**

- **Archetype Examples:** Warden (Tank/Melee), Arcanist (Ranged Magic/Control), Skirmisher (Mobile Damage/Debuffs), Healer (Support/Buffs).
- **Primer Skill Examples:**
    - `Frost Shard`: Deals minor cold damage and applies Chilled (Primer).
    - `Oily Flask`: Creates a patch of slippery oil (Primer).
    - `Mark of Vulnerability`: Target takes increased damage from next hit (Primer).
- **Catalyst Skill Examples:**
    - `Arcane Tether`: Links the Primed effect from one target to another nearby.
    - `Gust of Wind`: Spreads the area Primers or pushes enemies.
    - `Empowering Rune`: Next Detonator ability used on target with Mark of Vulnerability has increased area of effect.
- **Detonator Skill Examples:**
    - `Glacial Spike`: On a Chilled (Primer) target, deals massive cold damage.
    - `Immolate`: Ignites flammable Primers like Oily Flask, creating a lasting fire.
    - `Execution Strike`: Deals massive damage but slow, you need a CC Primer

**6. Core Gameplay Loop**

- **Micro Loop (Engaging an Enemy Group/Puzzle - 15-60 seconds):**
    1. Assess: Player observes, companions provide intel via UI/audio. Game state updates.
    2. Plan Presentation (Tactical Focus or Subtle Cues): HTN generates plans. Player can enter Tactical Focus to review, or UI subtly hints at the active plan's intent.
    3. Player Initiation/Adaptation: Player acts, their movement/skill use heavily influences which plan variant proceeds or if a quick re-plan is favored. Player may force a plan in Tactical Focus.
    4. Coordinated Action (Primer/Catalyst/Task): Player or companion, guided by the plan and RL, executes a task, potentially a Primer or Catalyst ability.
    5. Player/Companion Action (Detonator/Catalyst/Task): Another character completes an Attunement or executes another critical task.
    6. Resolution: Observe effects, adapt to outcome.
- **Macro Loop (Clearing a Level/Sector - 10-20 minutes):**
    1. Enter new area, receive objectives (combat, puzzle, NPC interaction).
    2. Navigate, overcome challenges using Attunements and strategic planning.
    3. Manage resources (health, cooldowns).
    4. Defeat area boss, solve major puzzle, or complete main objective with NPCs.
    5. Potentially unlock new swappable skills.
- **Meta Loop (Campaign Progression):**
    1. Advance main storyline.
    2. Unlock new skills, discover more complex Attunements.
    3. Tackle increasingly difficult challenges demanding deeper understanding of the AI planning system and intricate cooperation.

**7. Unique Selling Points**

- **AI as True Partners:** Companions are intelligent agents contributing to strategy formulation and execution.
- **Emergent Teamwork:** The interplay between HTN, RL, and the Attunement system creates dynamic, cooperative scenarios.
- **Player-Centric Strategy:** Despite advanced AI, the player is the decisive factor.
- **High Replayability:** Multiple plans, adaptable AI, rich skill system.
- **Transparent Strategy (via Tactical Focus):** Players can understand and influence AI decision-making.

Architecture

Unreal Engine is Windows based, other OS support is limited

OpenSpiel is Unix based, other OS support is limited

I am using InductorHtn as my C++ HTN engine, it can work on  any OS
The game  logic is in standard C++ with  STL containers. It’s wrapped in an OpenSpiel wrapper and I train RL models on Linux, that I export as ONNX. Both the logic and the ONNX  are embedded in Unreal 

Unreal is just the graphics/audio part. the entirety of the game logic is in the pure C++ engine. So, no pathfinding (it’s a 2D grid, I can trivially bypass it), no physics (except for visuals, like ragdolling a killed agent)

CRITICAL: This repo only goal is to handle to game logic in pure C++. The HTN, the RL training, Unreal, all three are out of the scope of this game