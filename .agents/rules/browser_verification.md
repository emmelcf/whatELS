# Browser Subagent Verification Protocol: Fail-Fast & Console-First

When spawning or executing tasks with `browser_subagent`:

## 1. Step 1 Is ALWAYS Console Check
- Immediately upon loading the URL, check for **browser console errors and warnings**.
- If ANY uncaught exception, `TypeError`, syntax error, or script error is detected:
  - **STOP IMMEDIATELY**.
  - Do NOT attempt to click elements, inspect other DOM nodes, or scroll.
  - Return immediately with the exact error message, stack trace, and file/line number.

## 2. Strict Fail-Fast Rule on Missing Elements
- Never guess alternate selectors or retry looping if an expected element is missing.
- If an element is not found on the first DOM inspection:
  - Check console logs once more.
  - Take a screenshot of the current viewport.
  - Return immediately stating which element was missing and what the current DOM/console state is.

## 3. Minimal Step Limits & Scoped Tasks
- Keep browser subagent tasks focused and bounded (maximum 3-5 quick actions).
- Avoid monolithic multi-feature integration scripts in a single subagent invocation.
- Once the primary objective (or failure) is determined, capture a screenshot and exit immediately.
