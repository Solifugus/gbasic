# gBASIC Site Sample Application Plan

This plan tracks a Postgres-backed gBASIC sample application for the eventual
gBASIC home site. The first goal is a credible local dogfood app. Public
deployment comes later, after the webserver, auth, and operational pieces are
hardened enough.

The client side should stay vanilla HTML, CSS, and JavaScript. Dependencies
should be added only when a specific feature has a strong argument that is not
worth maintaining in this repo.

## Principles

- Build the app in gBASIC and let it reveal missing standard-library pieces.
- Use Postgres for durable application data.
- Keep backend-specific database behavior visible instead of adding a generic
  SQL abstraction layer.
- Prefer small, testable helpers over a framework.
- Treat public deployment as an explicit hardening milestone, not a default
  assumption.

## Phase 0: Project Shape

- [x] Create `examples/gbasic_site/`.
- [x] Add `README.md` with local setup, Postgres requirements, and current
      limitations.
- [x] Add an app entry point, likely `site.bas`.
- [x] Add a local runner script under `tests/`.
- [x] Add the runner to docs but keep it opt-in if it needs a configured
      Postgres database.
- [x] Define the initial directory layout for app code, SQL, static assets,
      and tests.

Acceptance criteria:

- A developer can identify how to run the sample app locally.
- The project layout is stable enough for subsequent commits.

## Phase 1: Database Foundation

- [x] Add schema SQL for pages, forum categories, topics, posts, and moderation
      state.
- [x] Add seed data for a local demo.
- [x] Add a setup/reset script or gBASIC setup program for local development.
- [x] Decide how tests isolate data, such as a configurable schema prefix or
      temporary test tables.
- [x] Document required `PG*` environment variables.
- [x] Add a local Postgres setup helper based on standard libpq credentials.

Acceptance criteria:

- The app can initialize a local development database.
- Tests can reset their data without touching unrelated Postgres objects.

## Phase 2: Minimal Web Surface

- [x] Build a webserver loop using the existing request/response queue model.
- [x] Add routing helpers for method/path dispatch inside the app.
- [x] Add HTML escaping helpers.
- [ ] Add URL/form decoding helpers if the runtime does not already provide
      enough behavior.
- [x] Render Postgres-backed home, docs, and forum placeholder pages.
- [x] Render fuller docs/about/examples pages.
- [x] Serve minimal static CSS and JavaScript through gBASIC or a documented
      local reverse-proxy/static-file path.

Acceptance criteria:

- A browser can load the local home page.
- The page uses plain HTML/CSS/JS with no frontend build step.
- Basic routes return correct status codes for found and missing pages.

## Phase 3: Forum Prototype

- [x] Render forum category and topic lists.
- [x] Render topic detail pages with replies.
- [x] Add create-topic and create-reply forms.
- [x] Validate form input server-side.
- [x] Store posts in Postgres with parameterized SQL.
- [x] Add simple pagination or result limits before public deployment.

Acceptance criteria:

- A local user can create a topic and reply.
- Forum pages survive process restart because data lives in Postgres.

## Phase 4: Admin and Moderation

- [x] Add an admin-only moderation path.
- [x] Add delete/hide actions for posts and topics.
- [x] Add visible moderation state in the database.
- [x] Add audit fields such as created/updated/moderated timestamps.
- [x] Keep the first auth mechanism intentionally narrow until password/session
      support is designed.

Acceptance criteria:

- A moderator can hide inappropriate content locally.
- Ordinary forum views do not show hidden content.

## Phase 5: Test Coverage

- [x] Add loopback HTTP tests for public pages.
- [x] Add tests for create-topic and create-reply flows.
- [x] Add tests for validation failures.
- [x] Add tests for moderation behavior.
- [x] Add database reset/setup to the test runner.

Acceptance criteria:

- The sample app has an opt-in integration runner comparable to the Postgres
  and webserver runners.
- Tests document the intended request/response behavior.

## Phase 6: Production Hardening Inventory

- [x] Decide how the app runs behind nginx or Caddy.
- [x] Add bind-address/public-listener design work if gBASIC should serve
      directly beyond loopback.
- [x] Add request size and timeout review for forum forms.
- [x] Add logging plan.
- [x] Add backup/restore notes for Postgres.
- [x] Add spam prevention plan.
- [x] Add CSRF/session/password hashing design before real public accounts.
- [x] Add deployment checklist for the user's public-IP server.

Acceptance criteria:

- The project has an honest checklist of what remains before public exposure.
- Public deployment steps are documented but not required for local sample use.

## Open Questions

- Should the first live site be static plus links while the gBASIC app matures?
- Should docs content be checked-in HTML, markdown-like text rendered by
  gBASIC, or database-backed pages?
- Should static assets be served by gBASIC for dogfooding or by the reverse
  proxy for operational simplicity?
- What is the smallest acceptable auth model for the first moderation pass?
- Which missing web helpers belong in the standard library versus this sample
  app?
