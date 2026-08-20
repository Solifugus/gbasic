load chart from "../stdlib/chart.bas"
df = { q: ["Q1", "Q2"], v: [1, -2] }
s = chart.options(chart.y(chart.x(chart.spec("bar", df), "q"), "v"), { stacked: true })
print chart.render(s)
