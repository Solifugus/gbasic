program demo(args)
    print(env("GBASIC_ENV_TEST_VALUE"))
    print(is_unknown(env("GBASIC_ENV_TEST_MISSING")))
end program
