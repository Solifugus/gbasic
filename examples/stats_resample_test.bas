' Seedable PRNG + resampling (statistics_design.md §8 Phase 1). The generator
' is xoshiro256** over SplitMix64 — pure fixed-width integer math, so a given
' seed yields the same stream on every architecture, which this golden relies on.
' Values here are reproducible by construction (not matched to an external lib).
function avg(xs)
    return mean(xs)
end function

program demo(args)
    load stats from "../stdlib/stats.bas"

    ' Raw generator: a fixed seed gives a fixed stream.
    seed(42)
    print("r1 " + string(round(random(), 6)))
    print("r2 " + string(round(random(), 6)))
    print("i1 " + string(random_int(1, 6)))
    print("i2 " + string(random_int(1, 6)))
    print("i3 " + string(random_int(1, 6)))

    ' Reseeding reproduces the stream exactly.
    seed(42)
    print("r1again " + string(round(random(), 6)))

    ' Resampling. Reseed first so these are independent of the draws above.
    seed(2024)
    data = [10, 20, 30, 40, 50]

    sh = shuffle(data)
    print("shuffle " + string(sh))
    print("orig_intact " + string(data))
    print("shuffle_sum " + string(sum(sh)))

    s3 = sample(data, 3)
    print("sample3 " + string(s3))
    print("sample0 " + string(sample(data, 0)))
    print("sample_over " + string(sample(data, 9)))

    rs = resample(data, 8)
    print("resample8 " + string(rs))
    print("resample_len " + string(len(rs)))

    ' Bootstrap the sampling distribution of the mean.
    seed(99)
    bs = bootstrap(data, avg, 3000)
    print("boot_n " + string(len(bs)))
    print("boot_mean " + string(round(mean(bs), 3)))
    print("boot_se " + string(round(stdev(bs), 3)))
    print("boot_empty " + string(bootstrap([], avg, 10)))
end program
