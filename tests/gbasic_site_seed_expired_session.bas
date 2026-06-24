load pg

db = pg.connect({})

users = pg.query(db, "select id from gbasic_site_users where username = $1", ["site-admin"])
if count(users) = 0 then
    print("error: site-admin user not found")
else
    pg.exec(db, "insert into gbasic_site_sessions (id, user_id, csrf_token, expires_at) values ($1, $2, $3, now() - interval '1 hour') on conflict (id) do update set expires_at = now() - interval '1 hour', revoked_at = null", ["expired-session-test", users[0].id, "expired-csrf"])
    print("expired session seeded")
end if

pg.close(db)
