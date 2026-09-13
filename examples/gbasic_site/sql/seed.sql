-- The page copy. It described gBASIC as "a small readable programming
-- language for practical scripts" long after that stopped being the whole
-- truth, which is the same rot the docs gate exists to catch one layer out.
insert into gbasic_site_pages (slug, title, body) values
    ('home', 'gBASIC', 'A modern BASIC for business programming: familiar control flow, plus records, first-class functions, watchers, shared-nothing actors, and typed values for dates, durations and money. This page is served by a gBASIC program that is one server declaration long.'),
    ('docs', 'Docs', 'The tutorial teaches the language, the reference is the complete surface, and fifteen cookbooks work through real tasks: exact money across 178 currencies, double-entry accounting, loan servicing, spreadsheets read and recalculated in place, databases over ODBC, charts as deterministic SVG, dates and scheduling, and what a database estate says about itself. Every code block and every output block on a cookbook page is owned by a file the test suite runs and compares byte for byte.'),
    ('about', 'About gBASIC', 'gBASIC takes what BASIC got right, a program you can read aloud, and grows modern features where they pay for themselves. It is implemented as a tree-walking interpreter in C11 and ships as a single binary. Version 0.1.0 is an early release and the number is honest about that; it is not a sketch, since 127 test suites gate every change and the goldens are byte-exact. Platform is Linux.'),
    ('examples', 'Examples', 'The repository carries working programs for files, arrays, SQLite, PostgreSQL, web clients, web servers, spreadsheets, charts and native windows. They are not illustrations: the same programs are run by the test suite on every change, so an example that stopped working fails the build. This site is one of them.'),
    ('forum', 'Forum', 'A small PostgreSQL-backed discussion space, with sessions, per-IP rate limiting and cookie-bound CSRF tokens.');

insert into gbasic_site_categories (slug, title, description) values
    ('general', 'General', 'Questions, ideas, and project discussion.');

insert into gbasic_site_topics (category_id, title, author_name, body)
select id, 'Welcome to the gBASIC forum', 'site admin', 'This seeded topic proves the Postgres-backed forum tables are ready.'
from gbasic_site_categories
where slug = 'general';

insert into gbasic_site_posts (topic_id, author_name, body)
select id, 'site admin', 'Reply support is wired into the initial schema.'
from gbasic_site_topics
where title = 'Welcome to the gBASIC forum';
