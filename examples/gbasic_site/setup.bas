load pg

schema(file)= "examples/gbasic_site/sql/schema.sql"
reset(file)= "examples/gbasic_site/sql/reset.sql"
seed_sql(file)= "examples/gbasic_site/sql/seed.sql"

db = pg.connect({})

pg.exec(db, read(schema))
pg.exec(db, read(reset))
pg.exec(db, read(seed_sql))

admin_user = env("GBASIC_SITE_ADMIN_USER")
admin_password = env("GBASIC_SITE_ADMIN_PASSWORD")
if is_unknown(admin_user) or is_unknown(admin_password) then
    print("no admin user provisioned (set GBASIC_SITE_ADMIN_USER and GBASIC_SITE_ADMIN_PASSWORD)")
else
    admin_user(trimmed)= admin_user
    if admin_user = "" or admin_password = "" then
        print("no admin user provisioned (GBASIC_SITE_ADMIN_USER and GBASIC_SITE_ADMIN_PASSWORD must be non-empty)")
    else
        admin_hash = password_hash(admin_password)
        pg.exec(db, "insert into gbasic_site_users (username, password_hash, password_algorithm, admin, disabled) values ($1, $2, $3, true, false) on conflict (username) do update set password_hash = $2, password_algorithm = $3, admin = true, disabled = false, updated_at = now()", [admin_user, admin_hash, "crypt"])
        print("admin user provisioned: " + admin_user)
    end if
end if

print("gbasic_site database ready")
pg.close(db)
