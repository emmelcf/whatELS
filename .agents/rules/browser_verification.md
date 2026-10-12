# Browser Subagent Verification Protocol: Fail-Fast & Console-First

> **Core Objective:** Prevent token-burning loops where browser subagents waste hundreds of tool calls searching the DOM for missing elements when a JavaScript runtime exception has halted page rendering.

When spawning or executing tasks with `browser_subagent`:

## 1. Step 1 Is ALWAYS the Console Check
- Immediately upon loading the page or URL, check for **browser console errors and uncaught exceptions**.
- If ANY script error, `TypeError`, `ReferenceError`, syntax error, or network fetch failure is detected:
  - **STOP IMMEDIATELY.**
  - Do **NOT** attempt to click elements, inspect the DOM tree, scroll, or execute follow-up actions.
  - Return immediately with the exact error message, stack trace, and the culprit script file/line number.
  - The UI cannot be reliably tested while scripts are in a broken runtime state.

## 2. Strict Fail-Fast Rule on Missing Elements (Preventing DOM Hunting)
- When a prompt directs the browser subagent to interact with or verify an element that does not appear in the initial DOM:
  - **Check console logs first:** Almost always, a missing element is an unrendered component caused by an upstream JavaScript exception that silently aborted script execution.
  - **Zero Selector Guessing:** Never guess alternative selectors, fuzzy-match labels, or trigger retry loops.
  - Capture a screenshot of the current viewport.
  - Exit immediately, reporting:
    1. Which element selector was missing.
    2. Any errors logged in the console.
    3. The captured viewport screenshot.

## 3. Minimal Step Limits & Scoped Verification
- Keep browser subagent tasks strictly bounded (maximum 3–5 quick actions per invocation).
- Avoid monolithic multi-feature test scripts in a single subagent invocation.
- As soon as the primary assertion passes or fails, capture a screenshot and terminate the subagent session.
