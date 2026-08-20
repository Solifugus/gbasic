load chart from "../stdlib/chart.bas"
df = { q: ["Q1", "Q1"], v: [1, 2] }
print chart.bar(df, "q", "v")
