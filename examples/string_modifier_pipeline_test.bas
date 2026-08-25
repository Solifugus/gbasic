raw = "  apple,banana,orange  "
clean{trimmed}= raw
words{split ","}= clean
line{join " | "}= words
print(line)
