insert into gbasic_site_pages (slug, title, body) values
    ('home', 'gBASIC', 'Readable programs, practical web experiments.'),
    ('docs', 'Docs', 'Guides and examples for learning gBASIC.'),
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
