load chart from "../stdlib/chart.bas"
df = { m: [1, 2], name: ["a", "b"] }
print chart.line(df, "m", "name")
