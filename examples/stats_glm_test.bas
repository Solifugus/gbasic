' stats.bas — GLM family expansion for social / communication science
' (probit, negative binomial, ordinal logistic, multinomial logistic). Verified
' against statsmodels on a fixed 80-observation dataset with two predictors
' (x1, x2) and binary (yb), count (yc), ordinal (yo), and nominal (ym) outcomes.
' gBASIC prints ~6 sig figs, so results round to 4-5 decimals.
program demo(args)
    load stats from "../stdlib/stats.bas"

    x1 = [0.4412, -0.3309, 2.4308, -0.2521, 0.1096, 1.5825, -0.9092, -0.5916, 0.1876, -0.3299, -1.1928, -0.2049, -0.3588, 0.6035, -1.6648, -0.7002, 1.1514, 1.8573, -1.5112, 0.6448, -0.9806, -0.8569, -0.8719, -0.4225, 0.9964, 0.7124, 0.0591, -0.3633, 0.0033, -0.1059, 0.7931, -0.6316, -0.0062, -0.1011, -0.0523, 0.2492, 0.1977, 1.3348, -0.0869, 1.5615, -0.3059, -0.4777, 0.1007, 0.3554, 0.2696, 1.292, 1.1393, 0.4944, -0.3363, -0.1006, 1.4134, 0.2213, -1.3108, -0.6896, -0.5775, 1.1522, -0.1072, 2.2601, 0.6566, 0.1248, -0.4357, 0.9722, -0.2407, -0.8241, 0.5681, 0.0128, 1.1891, -0.0736, -2.8597, 0.7894, -1.8777, 1.5388, 1.8214, -0.427, -1.1647, -1.3971, 0.8727, -0.2021, -0.5984, -0.2434]
    x2 = [1.0534, 1.3217, 1.786, 1.9303, 1.5399, 1.5182, 1.4201, 1.4031, 1.5347, 1.9487, 0.7474, 0.1661, 0.4793, 0.443, 0.7272, 1.6206, 0.1202, 0.8995, 1.6263, 0.5285, 0.1268, 0.4842, 0.1701, 1.6156, 0.3405, 0.3907, 1.6293, 1.6206, 1.1787, 1.8295, 0.1196, 1.93, 1.142, 0.605, 1.6514, 1.3188, 1.973, 0.215, 1.1618, 0.9457, 1.3045, 0.4837, 0.0623, 1.0885, 0.7294, 1.7847, 0.9187, 0.8373, 1.2658, 1.0544, 1.9224, 1.578, 0.9934, 0.4223, 1.208, 1.4972, 1.5117, 1.987, 0.437, 0.8513, 0.8477, 0.6373, 0.7307, 0.9559, 1.0842, 0.5305, 0.2687, 0.6038, 0.273, 0.6354, 1.3584, 1.2027, 1.9945, 1.1218, 1.0975, 1.2847, 1.4536, 1.2316, 1.177, 1.21]
    yb = [1, 0, 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 1, 0, 1, 0, 1, 0, 0, 1, 1, 1, 0, 0, 0, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1, 0, 0, 1, 0, 1, 1, 1, 1, 1, 0, 1, 1, 0, 1, 1, 0, 1, 1, 1, 1, 0, 0, 0, 1, 1, 1, 1, 0, 0, 1, 0, 1, 1, 1, 0, 0, 1, 0, 0, 1]
    yc = [0, 0, 2, 0, 1, 0, 0, 0, 0, 1, 0, 2, 0, 2, 0, 0, 1, 1, 0, 2, 0, 0, 0, 1, 6, 3, 4, 0, 0, 1, 2, 0, 1, 1, 2, 0, 0, 3, 0, 2, 0, 0, 2, 2, 0, 0, 2, 2, 0, 2, 4, 0, 0, 0, 0, 2, 2, 4, 3, 1, 0, 0, 1, 0, 1, 4, 4, 0, 0, 0, 0, 1, 0, 0, 0, 1, 1, 0, 0, 1]
    yo = [1, 0, 2, 0, 1, 1, 0, 0, 0, 0, 0, 2, 0, 2, 0, 0, 0, 1, 1, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 1, 0, 2, 0, 0, 2, 0, 0, 0, 2, 0, 0, 1, 0, 2, 0, 1, 1, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 1, 0, 0, 0, 0, 0, 0, 1, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0]
    ym = [2, 2, 1, 2, 2, 0, 2, 0, 2, 2, 0, 1, 0, 1, 2, 2, 0, 1, 2, 1, 2, 2, 1, 2, 1, 2, 2, 0, 1, 0, 1, 2, 2, 2, 2, 0, 0, 1, 1, 1, 2, 0, 0, 0, 2, 0, 2, 0, 2, 0, 2, 0, 2, 1, 2, 1, 2, 2, 2, 2, 2, 1, 1, 1, 0, 0, 1, 0, 2, 1, 1, 1, 0, 0, 0, 2, 0, 1, 2, 1]

    ' Reporting helpers on a logistic fit. statsmodels Logit: pseudo-R2 0.21697;
    ' x1 odds ratio 4.6674 (95% CI 2.0482-10.6362), coef CI 0.717-2.3643.
    lg = logistic_regression(yb, [x1, x2])
    print("logit prsq " + string(round(lg.pseudo_r2, 5)))
    orr = odds_ratios(lg, 0.95)
    print("logit or1 " + string(round(orr[1].odds_ratio, 4)) + " ci " + string(round(orr[1].ci_low, 4)) + " " + string(round(orr[1].ci_high, 4)))
    ci = conf_int(lg, 0.95)
    print("logit ci1 " + string(round(ci[1].low, 4)) + " " + string(round(ci[1].high, 4)))
    ' Average marginal effects. statsmodels margeff(overall): logit
    ' AME 0.25775 -0.00957, SE 0.0462 0.08834.
    me = marginal_effects(lg, [x1, x2])
    print("logit ame " + string(round(me.effects[0], 5)) + " " + string(round(me.effects[1], 5)) + " se " + string(round(me.std_errors[0], 5)) + " " + string(round(me.std_errors[1], 5)))

    ' Probit. statsmodels GLM(probit): beta 0.5418 0.8966 -0.0523,
    ' se 0.3685 0.2257 0.3101, llf -40.0556, aic 86.1113.
    pb = probit_regression(yb, [x1, x2])
    print("probit b " + string(round(pb.coefficients[0], 4)) + " " + string(round(pb.coefficients[1], 4)) + " " + string(round(pb.coefficients[2], 4)))
    print("probit se " + string(round(pb.std_errors[0], 4)) + " " + string(round(pb.std_errors[1], 4)) + " " + string(round(pb.std_errors[2], 4)))
    print("probit ll " + string(round(pb.log_likelihood, 4)) + " aic " + string(round(pb.aic, 4)))
    ' Probit AME. Effects match statsmodels (0.25399 -0.01481); the SEs use the
    ' expected-information covariance (like GLM), so they differ slightly from
    ' sm.Probit's observed-Hessian margeff SEs, as the coefficient SEs do.
    pme = marginal_effects(pb, [x1, x2])
    print("probit ame " + string(round(pme.effects[0], 5)) + " " + string(round(pme.effects[1], 5)) + " se " + string(round(pme.std_errors[0], 5)) + " " + string(round(pme.std_errors[1], 5)))

    ' Negative binomial (NB2). statsmodels: beta 0.1837 0.7963 -0.5248,
    ' se 0.2587 0.1534 0.2282, alpha 0.242, llf -95.7091, aic 199.4183.
    nb = negbinom_regression(yc, [x1, x2])
    print("negbin b " + string(round(nb.coefficients[0], 4)) + " " + string(round(nb.coefficients[1], 4)) + " " + string(round(nb.coefficients[2], 4)))
    print("negbin se " + string(round(nb.std_errors[0], 4)) + " " + string(round(nb.std_errors[1], 4)) + " " + string(round(nb.std_errors[2], 4)))
    print("negbin alpha " + string(round(nb.alpha, 4)) + " ll " + string(round(nb.log_likelihood, 4)) + " aic " + string(round(nb.aic, 4)))

    ' Ordinal logistic (proportional odds). statsmodels OrderedModel:
    ' slopes 0.9366 -0.8446, se 0.3019 0.4798, cutpoints 0.2655 1.8826,
    ' llf -55.4391.
    orl = ordinal_regression(yo, [x1, x2])
    print("ordinal b " + string(round(orl.coefficients[0], 4)) + " " + string(round(orl.coefficients[1], 4)))
    print("ordinal se " + string(round(orl.std_errors[0], 4)) + " " + string(round(orl.std_errors[1], 4)))
    print("ordinal cuts " + string(round(orl.cutpoints[0], 4)) + " " + string(round(orl.cutpoints[1], 4)) + " ll " + string(round(orl.log_likelihood, 4)))
    print("ordinal prsq " + string(round(orl.pseudo_r2, 5)))

    ' Multinomial logistic (baseline category 0). statsmodels MNLogit:
    ' cat1 [1.0929 0.4915 -1.3096], cat2 [-0.3007 -0.5946 0.5669],
    ' llf -76.3001, aic 164.6002.
    mn = multinomial_regression(ym, [x1, x2])
    print("mnl cat1 " + string(round(mn.coefficients[0][0], 4)) + " " + string(round(mn.coefficients[0][1], 4)) + " " + string(round(mn.coefficients[0][2], 4)))
    print("mnl cat2 " + string(round(mn.coefficients[1][0], 4)) + " " + string(round(mn.coefficients[1][1], 4)) + " " + string(round(mn.coefficients[1][2], 4)))
    print("mnl se1 " + string(round(mn.std_errors[0][0], 4)) + " " + string(round(mn.std_errors[0][1], 4)) + " " + string(round(mn.std_errors[0][2], 4)))
    print("mnl ll " + string(round(mn.log_likelihood, 4)) + " aic " + string(round(mn.aic, 4)))
    print("mnl prsq " + string(round(mn.pseudo_r2, 5)))

    ' domain guards return unknown
    print("guard_ord " + string(ordinal_regression([0, 1], [])))
    print("guard_mnl " + string(multinomial_regression([0, 1], [[1, 2]])))
end program
