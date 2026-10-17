' A prefix is a NAME, not a path: a separator would put the file somewhere the
' caller did not name.
print temp_file({ prefix: "../escaped" })
