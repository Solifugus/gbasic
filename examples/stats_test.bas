' Phase 1 statistics foundation: elementary math + dispersion/shape reductions.
' Data set [2,4,4,4,5,5,7,9] has mean 5, population variance 4, population stdev 2.
xs = [2, 4, 4, 4, 5, 5, 7, 9]

print("mean " + string(mean(xs)))
print("pvariance " + string(pvariance(xs)))
print("pstdev " + string(pstdev(xs)))
print("variance " + string(round(variance(xs), 6)))
print("stdev " + string(round(stdev(xs), 6)))
print("skewness " + string(skewness(xs)))
print("kurtosis " + string(kurtosis(xs)))

print("sqrt " + string(sqrt(144)))
print("abs " + string(abs(0 - 3.5)))
print("exp0 " + string(exp(0)))
print("log " + string(round(log(exp(1)), 6)))
print("log10 " + string(log10(1000)))
print("floor " + string(floor(2.7)))
print("ceil " + string(ceil(2.1)))
print("pow " + string(pow(2, 10)))
print("sign " + string(sign(0 - 42)))

' Order statistics and paired measures.
ys = [1, 3, 5, 4, 6, 5, 8, 9]
print("range " + string(range(xs)))
print("iqr " + string(iqr(xs)))
print("q25 " + string(quantile(xs, 0.25)))
print("median_q " + string(quantile(xs, 0.5)))
print("q75 " + string(quantile(xs, 0.75)))
print("p90 " + string(percentile(xs, 90)))
print("covariance " + string(round(covariance(xs, ys), 6)))
print("correlation " + string(round(correlation(xs, ys), 6)))
