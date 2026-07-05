program main(args)
    load llm from "../stdlib/llm.bas"
    ' WP-LLM-1: the offline fixture transport must RAISE on a missing fixture
    ' (mirrors the edgar offline seam), not silently succeed.
    m = llm.openai("gpt-4o", "sk-test")
    m = llm.offline(m, "examples/fixtures/llm_empty")
    print(llm.ask(m, "sys", "go"))
end program
