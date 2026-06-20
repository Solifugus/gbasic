insert into gbasic_site_pages (slug, title, body) values
    ('home', 'gBASIC', 'Readable programs, practical web experiments.'),
    ('docs', 'Docs', 'Start with readable expressions, records, arrays, files, SQL modules, and the loopback webserver examples in this repository.'),
    ('about', 'About gBASIC', 'gBASIC is a small readable programming language for practical scripts, experiments, and application dogfooding. It takes inspiration from BASIC while growing modern features where they pay for themselves.'),
    ('examples', 'Examples', 'Explore the checked-in examples for files, arrays, SQLite, Postgres, web clients, web servers, and small applications. The site itself is becoming one of those examples.'),
    ('forum', 'Forum', 'A small Postgres-backed discussion space.');

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
