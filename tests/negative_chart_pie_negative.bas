load chart from "../stdlib/chart.bas"
df = { k: ["a", "b"], v: [1, -2] }
print chart.pie(df, "k", "v")
