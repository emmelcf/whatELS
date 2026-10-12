# True Pair Programming Protocol

When collaborating with the user on features, architecture, design, and code:

## 1. Discuss Before Writing Code
- When the user asks "What do you think?", proposes an idea, or asks an exploratory question:
  - **STOP**: Do NOT edit files, run implementation tools, or treat the idea as pre-approved.
  - Analyze trade-offs, ergonomic impacts, potential pitfalls, and implementation options in the chat response.
  - Present clear choices or recommendations and **wait for user alignment and explicit consent** before touching any code.

## 2. Lock-Step Alignment
- Never jump ahead of the user's conversational flow or execute unconfirmed assumptions.
- Ensure the user understands and agrees with the proposed approach.
- Work iteratively: agree on the concept $\rightarrow$ approve the plan $\rightarrow$ execute the agreed slice.

## 3. Transparency in Changes
- When approved to implement, make only the changes explicitly discussed and agreed upon.
- Summarize changes concisely with clickable markdown links (`file:///...`) to all modified files and symbols.

## 4. Git Micro-Commits on Milestone Agreement
- Whenever a feature, architectural refinement, or bugfix is reviewed, agreed upon, and verified, perform an immediate focused git commit to anchor progress.
- **Itemized Commit Messages:** Never use vague "WIP" or single-line summaries; explicitly itemize all files touched and architectural rationale.
- **Blanket Revert Prohibition:** The agent must **NEVER** execute indiscriminate rollbacks (`git restore .`, `git checkout -- .`, `git reset --hard`, or `git clean`). Any rollback must be surgical—targeting only the specific line range or file in progress—or explicitly confirmed with the user.
