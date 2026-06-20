load pg

db = pg.connect({})

pages = pg.query(db, "select slug, title from gbasic_site_pages order by slug")
print(len(pages))

home = pg.query(db, "select title from gbasic_site_pages where slug = $1", ["home"])
print(home[0].title)

examples = pg.query(db, "select title from gbasic_site_pages where slug = $1", ["examples"])
print(examples[0].title)

categories = pg.query(db, "select slug, title from gbasic_site_categories where hidden = false order by slug")
print(len(categories))
print(categories[0].slug)
print(categories[0].title)

topics = pg.query(db, "select t.title, t.moderated_by, c.slug as category_slug from gbasic_site_topics t join gbasic_site_categories c on c.id = t.category_id where t.hidden = false order by t.id")
print(len(topics))
print(topics[0].category_slug)
print(topics[0].title)
print(is_nothing(topics[0].moderated_by))

posts = pg.query(db, "select body, moderated_by from gbasic_site_posts where hidden = false order by id")
print(len(posts))
print(posts[0].body)
print(is_nothing(posts[0].moderated_by))

print(pg.close(db))
