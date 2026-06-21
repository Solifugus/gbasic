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

users = pg.query(db, "insert into gbasic_site_users (username, password_hash, password_algorithm, admin) values ($1, $2, $3, $4) returning id, username, admin, disabled", ["admin", "not-a-production-hash", "placeholder", true])
print(users[0].username)
print(users[0].admin)
print(users[0].disabled)

sessions = pg.query(db, "insert into gbasic_site_sessions (id, user_id, csrf_token, expires_at) values ($1, $2, $3, now() + interval '1 hour') returning id, csrf_token, revoked_at", ["session-test", users[0].id, "csrf-test"])
print(sessions[0].id)
print(sessions[0].csrf_token)
print(is_nothing(sessions[0].revoked_at))

joined = pg.query(db, "select u.username from gbasic_site_sessions s join gbasic_site_users u on u.id = s.user_id where s.id = $1", ["session-test"])
print(joined[0].username)

print(pg.close(db))
