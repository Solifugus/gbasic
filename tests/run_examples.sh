#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")/.."

cleanup_example_artifacts() {
    rm -f \
        examples/tmp_error_lock_test.txt \
        examples/tmp_file_test.txt \
        examples/tmp_lock_cleanup_test.txt \
        examples/tmp_lock_test.txt \
        examples/tmp_edgar_test.db \
        examples/tmp_edgar_facts.db \
        examples/tmp_edgar_demo.db \
        examples/tmp_screener_a.db \
        examples/tmp_screener_b.db \
        examples/tmp_screener_scores.db \
        examples/tmp_scorecard_cache.db \
        examples/tmp_xml_reader.xml
}

trap cleanup_example_artifacts EXIT

make clean
make

export GBASIC_ENV_TEST_VALUE=configured
unset GBASIC_ENV_TEST_MISSING

examples=(
    arithmetic_number_modifier_test.bas
    array_append_prepend_test.bas
    array_cow_test.bas
    array_insert_remove_test.bas
    array_reverse_test.bas
    array_sort_test.bas
    array_take_test.bas
    array_unique_test.bas
    stats_test.bas
    stats_normal_test.bas
    stats_dist_test.bas
    matrix_test.bas
    stats_ols_test.bas
    stats_resample_test.bas
    stats_inference_test.bas
    stats_cluster_test.bas
    stats_timeseries_test.bas
    stats_correlation_test.bas
    stats_distribution_test.bas
    stats_expdesign_test.bas
    stats_power_test.bas
    stats_reliability_test.bas
    stats_proportions_test.bas
    stats_optimize_test.bas
    stats_arima_test.bas
    stats_arima_mle_test.bas
    stats_garch_test.bas
    stats_glm_test.bas
    stats_glm_factors_test.bas
    stats_robust_se_test.bas
    stats_mediation_test.bas
    stats_econ_test.bas
    cookbook_social_test.bas
    cookbook_econ_test.bas
    epoch_test.bas
    bitwise_test.bas
    monotonic_test.bas
    modifier_escape_test.bas
    clause_recognition_test.bas
    persist_test.bas
    ari_teller_test.bas
    ari_delinquency_test.bas
    xlsx_read_test.bas
    xlsx_write_test.bas
    xlsx_formula_test.bas
    xlsx_recalc_test.bas
    xlsx_macro_sheet_test.bas
    xlsx_shared_formula_test.bas
    xlsx_modern_test.bas
    xlsx_textmath_test.bas
    xlsx_crosssheet_test.bas
    xlsx_workbook_recalc_test.bas
    filetree_test.bas
    hex_literal_test.bas
    stats_frame_test.bas
    type_builtin_test.bas
    conversion_builtin_test.bas
    count_builtin_test.bas
    string_concat_test.bas
    parse_test.gb
    record_test.gb
    record_helpers_test.bas
    record_keyword_keys_test.bas
    datetime_test.gb
    datetime_exact_comparison_test.bas
    datetime_arithmetic_test.bas
    datetime_lens_test.bas
    datetime_lens_operator_test.bas
    datetime_modifier_test.bas
    dates_lib_test.bas
    keyword_stability_test.bas
    duration_test.gb
    now_test.bas
    file_test.gb
    file_management_test.gb
    directory_management_test.gb
    file_overwrite_test.gb
    nap_fs_test.gb
    path_utilities_test.gb
    read_lines_test.gb
    find_test.bas
    function_call_comparison_test.bas
    lock_test.gb
    lock_cleanup_test.gb
    lower_upper_modifier_test.bas
    dir_test.gb
    function_test.gb
    goto_test.gb
    gosub_test.gb
    helper_functions_test.bas
    if_else_test.bas
    inline_if_test.bas
    watch_test.gb
    watch_path_test.bas
    watch_symmetric_path_test.bas
    watcher_value_change_guard_test.bas
    watcher_cascade_dedup_test.bas
    watcher_cycle_error_test.bas
    watcher_mutator_notification_test.bas
    while_test.bas
    while_break_continue_test.bas
    for_each_test.bas
    for_range_test.bas
    do_loop_test.bas
    error_test.gb
    on_error_resume_next_test.bas
    modifier_test.gb
    modifier_library_regression_test.bas
    modifier_string_helpers_test.bas
    negated_comparison_test.bas
    money_test.bas
    multiline_string_test.bas
    nothing_test.bas
    number_string_modifier_test.bas
    consider_test.bas
    comparison_lens_test.bas
    comparison_lens_parser_hardening_test.bas
    dynamic_record_access_test.bas
    pbi_policy_parse_test.bas
    pbi_derive_test.bas
    pbi_cow_test.bas
    env_builtin_test.bas
    sleep_test.bas
    edgar_offline_test.bas
    edgar_facts_poll_test.bas
    fundamentals_series_test.bas
    fundamentals_derived_test.bas
    edgar/fundamentals_demo.bas
    forensics_scores_test.bas
    forensics_distress_test.bas
    forensics_beneish_test.bas
    forensics_flags_test.bas
    forensics_events_test.bas
    edgar/monitor_harness_test.bas
    edgar/scorecard.bas
    xml_parse_test.bas
    xml_form4_test.bas
    xml_encode_test.bas
    xml_reader_test.bas
    xml_window_13f_test.bas
    xml_parse_html_test.bas
    insiders_form4_test.bas
    insiders_cluster_test.bas
    ownership_13f_test.bas
    ownership_stakes_test.bas
    llm_adapter_test.bas
    llm_retry_test.bas
    llm_ask_json_test.bas
    llm_tools_test.bas
    json_strict_test.bas
    mdna_sections_test.bas
    mdna_prepass_test.bas
    mdna_panel_test.bas
    screener_ingest_test.bas
    screener_score_test.bas
    password_hash_test.bas
    secure_token_test.bas
    nested_lvalue_test.bas
    nested_array_mutation_test.bas
    parser_hardening_test.bas
    print_parens_test.bas
    split_find_join_integration_test.bas
    split_join_test.bas
    serialization_test.bas
    serialize_roundtrip_test.bas
    actor_loopback_test.bas
    spawn_echo_test.bas
    spawn_fanout_test.bas
    spawn_loop_test.bas
    spawn_handle_passing_test.bas
    spawn_selective_receive_test.bas
    spawn_receive_timeout_test.bas
    spawn_monitor_test.bas
    supervisor_test.bas
    send_link_lenient_test.bas
    first_class_function_test.bas
    method_test.bas
    method_attach_test.bas
    this_method_test.bas
    reflect_test.bas
    chained_method_test.bas
    constructor_test.bas
    function_serialize_test.bas
    spawn_function_test.bas
    quote_test.bas
    string_modifier_pipeline_test.bas
    string_helpers_test.bas
    chr_code_test.bas
    unicode_bytes_test.bas
    unicode_chars_test.bas
    unicode_case_test.bas
    string_test.bas
    unknown_test.bas
    unknown_nothing_distinction_integration_test.bas
    sqlite_module_test.bas
    postgres_module_test.bas
    builtin_test.bas
    builtin_override_test.bas
    program_test.bas
    args_test.bas
    library_test.bas
    load_test.bas
    load_from_test.bas
    use_test.bas
    use_from_test.bas
    use_search_test.bas
    qualified_modifier_test.bas
    qualified_function_test.bas
)

for example in "${examples[@]}"; do
    path="examples/$example"
    expected="${path%.*}.out"
    stdout_file="$(mktemp)"
    stderr_file="$(mktemp)"

    if ./gbasic "$path" >"$stdout_file" 2>"$stderr_file"; then
        if [[ -f "$expected" ]]; then
            actual_text="$(cat "$stdout_file")"
            expected_text="$(cat "$expected")"
            if [[ "$actual_text" == "$expected_text" ]]; then
                printf 'PASS %s\n' "$path"
            else
                printf 'FAIL %s\n' "$path"
                printf 'stdout mismatch against %s\n' "$expected"
                actual_norm="$(mktemp)"
                expected_norm="$(mktemp)"
                printf '%s\n' "$actual_text" >"$actual_norm"
                printf '%s\n' "$expected_text" >"$expected_norm"
                diff -u "$expected_norm" "$actual_norm" || true
                rm -f "$actual_norm" "$expected_norm" "$stdout_file" "$stderr_file"
                exit 1
            fi
        else
            printf 'PASS %s\n' "$path"
        fi
        rm -f "$stdout_file" "$stderr_file"
    else
        status=$?
        # An OPTIONAL-DEPENDENCY build, not a defect. The Makefile compiles each
        # optional module out when its library is absent and the runtime then
        # raises a fixed "not available in this build" error -- that degradation
        # IS the documented contract (CLAUDE.md, "Conventions"). This list, by
        # contrast, is unconditional, so on a build without zlib/libxml2/sqlite/
        # libpq/libcurl/libxcrypt those cases failed the suite for doing exactly
        # what they promise: measured 34 of 182 examples on a no-optional-deps
        # build, i.e. the minimal configuration could not pass its own tests.
        #
        # Matched on the runtime's own message rather than a name->capability
        # table on purpose: a table is a second list to keep in step with the
        # first, and it silently rots when an example starts using a new module.
        # The message is emitted only by the `#if HAVE_*` guards, so it cannot be
        # produced by a genuine failure of the feature under test.
        if [[ -s "$stderr_file" ]] && grep -qE 'support is (not available in this build|unavailable)' "$stderr_file"; then
            printf 'SKIP %s (%s)\n' "$path" "$(sed -n 's/.*: \([A-Za-z-]* support is [^;]*\).*/\1/p' "$stderr_file" | head -1)"
            rm -f "$stdout_file" "$stderr_file"
            continue
        fi
        printf 'FAIL %s\n' "$path"
        if [[ -s "$stderr_file" ]]; then
            cat "$stderr_file"
        fi
        rm -f "$stdout_file" "$stderr_file"
        exit "$status"
    fi
done

# Program arguments after the script path (multi-arg case; the zero-arg case runs
# in the loop above via args_test.bas). Invoked explicitly so trailing args reach
# the program.
args_multi_stdout="$(mktemp)"
if ./gbasic examples/args_test.bas alpha beta gamma >"$args_multi_stdout" 2>/dev/null; then
    if [[ "$(cat "$args_multi_stdout")" == "$(cat examples/args_multi_test.out)" ]]; then
        printf 'PASS %s\n' "examples/args_test.bas alpha beta gamma"
    else
        printf 'FAIL %s\n' "examples/args_test.bas alpha beta gamma"
        diff -u examples/args_multi_test.out "$args_multi_stdout" || true
        rm -f "$args_multi_stdout"
        exit 1
    fi
else
    status=$?
    printf 'FAIL %s\n' "examples/args_test.bas alpha beta gamma"
    rm -f "$args_multi_stdout"
    exit "$status"
fi
rm -f "$args_multi_stdout"

help_file="$(mktemp)"
if ./gbasic --help >"$help_file"; then
    printf 'PASS %s\n' "gbasic --help"
else
    status=$?
    printf 'FAIL %s\n' "gbasic --help"
    rm -f "$help_file"
    exit "$status"
fi
rm -f "$help_file"

version_file="$(mktemp)"
if ./gbasic --version >"$version_file"; then
    version_text="$(cat "$version_file")"
    if [[ "$version_text" == "gBASIC 0.1.0-rc1" ]]; then
        printf 'PASS %s\n' "gbasic --version"
    else
        printf 'FAIL %s\n' "gbasic --version"
        printf 'expected: gBASIC 0.1.0-rc1\n'
        printf 'actual: %s\n' "$version_text"
        rm -f "$version_file"
        exit 1
    fi
else
    status=$?
    printf 'FAIL %s\n' "gbasic --version"
    rm -f "$version_file"
    exit "$status"
fi
rm -f "$version_file"

stdout_file="$(mktemp)"
stderr_file="$(mktemp)"
if printf 'Ada\n' | ./gbasic examples/input_test.bas >"$stdout_file" 2>"$stderr_file"; then
    actual_text="$(cat "$stdout_file")"
    expected_text="$(cat examples/input_test.out)"
    if [[ "$actual_text" == "$expected_text" ]]; then
        printf 'PASS %s\n' "examples/input_test.bas"
    else
        printf 'FAIL %s\n' "examples/input_test.bas"
        actual_norm="$(mktemp)"
        expected_norm="$(mktemp)"
        printf '%s\n' "$actual_text" >"$actual_norm"
        printf '%s\n' "$expected_text" >"$expected_norm"
        diff -u "$expected_norm" "$actual_norm" || true
        rm -f "$actual_norm" "$expected_norm" "$stdout_file" "$stderr_file"
        exit 1
    fi
else
    status=$?
    printf 'FAIL %s\n' "examples/input_test.bas"
    if [[ -s "$stderr_file" ]]; then
        cat "$stderr_file"
    fi
    rm -f "$stdout_file" "$stderr_file"
    exit "$status"
fi
rm -f "$stdout_file" "$stderr_file"

stdout_file="$(mktemp)"
stderr_file="$(mktemp)"
if printf '  Joe Jones  \n' | ./gbasic examples/input_trimmed_integration_test.bas >"$stdout_file" 2>"$stderr_file"; then
    actual_text="$(cat "$stdout_file")"
    expected_text="$(cat examples/input_trimmed_integration_test.out)"
    if [[ "$actual_text" == "$expected_text" ]]; then
        printf 'PASS %s\n' "examples/input_trimmed_integration_test.bas"
    else
        printf 'FAIL %s\n' "examples/input_trimmed_integration_test.bas"
        actual_norm="$(mktemp)"
        expected_norm="$(mktemp)"
        printf '%s\n' "$actual_text" >"$actual_norm"
        printf '%s\n' "$expected_text" >"$expected_norm"
        diff -u "$expected_norm" "$actual_norm" || true
        rm -f "$actual_norm" "$expected_norm" "$stdout_file" "$stderr_file"
        exit 1
    fi
else
    status=$?
    printf 'FAIL %s\n' "examples/input_trimmed_integration_test.bas"
    if [[ -s "$stderr_file" ]]; then
        cat "$stderr_file"
    fi
    rm -f "$stdout_file" "$stderr_file"
    exit "$status"
fi
rm -f "$stdout_file" "$stderr_file"

stdout_file="$(mktemp)"
stderr_file="$(mktemp)"
if printf '41\n' | ./gbasic examples/input_number_integration_test.bas >"$stdout_file" 2>"$stderr_file"; then
    actual_text="$(cat "$stdout_file")"
    expected_text="$(cat examples/input_number_integration_test.out)"
    if [[ "$actual_text" == "$expected_text" ]]; then
        printf 'PASS %s\n' "examples/input_number_integration_test.bas"
    else
        printf 'FAIL %s\n' "examples/input_number_integration_test.bas"
        actual_norm="$(mktemp)"
        expected_norm="$(mktemp)"
        printf '%s\n' "$actual_text" >"$actual_norm"
        printf '%s\n' "$expected_text" >"$expected_norm"
        diff -u "$expected_norm" "$actual_norm" || true
        rm -f "$actual_norm" "$expected_norm" "$stdout_file" "$stderr_file"
        exit 1
    fi
else
    status=$?
    printf 'FAIL %s\n' "examples/input_number_integration_test.bas"
    if [[ -s "$stderr_file" ]]; then
        cat "$stderr_file"
    fi
    rm -f "$stdout_file" "$stderr_file"
    exit "$status"
fi
rm -f "$stdout_file" "$stderr_file"

stdout_file="$(mktemp)"
stderr_file="$(mktemp)"
if printf 'Ada\n41\n' | ./gbasic examples/input_birth_year_concat_test.bas >"$stdout_file" 2>"$stderr_file"; then
    actual_text="$(cat "$stdout_file")"
    expected_text="$(cat examples/input_birth_year_concat_test.out)"
    if [[ "$actual_text" == "$expected_text" ]]; then
        printf 'PASS %s\n' "examples/input_birth_year_concat_test.bas"
    else
        printf 'FAIL %s\n' "examples/input_birth_year_concat_test.bas"
        actual_norm="$(mktemp)"
        expected_norm="$(mktemp)"
        printf '%s\n' "$actual_text" >"$actual_norm"
        printf '%s\n' "$expected_text" >"$expected_norm"
        diff -u "$expected_norm" "$actual_norm" || true
        rm -f "$actual_norm" "$expected_norm" "$stdout_file" "$stderr_file"
        exit 1
    fi
else
    status=$?
    printf 'FAIL %s\n' "examples/input_birth_year_concat_test.bas"
    if [[ -s "$stderr_file" ]]; then
        cat "$stderr_file"
    fi
    rm -f "$stdout_file" "$stderr_file"
    exit "$status"
fi
rm -f "$stdout_file" "$stderr_file"

stdout_file="$(mktemp)"
stderr_file="$(mktemp)"
if printf 'help\nnorth\nnorth\nlook\nsouth\nsouth\ntake lamp\ngo north\nnorth\nlook\neast\nlook\ntake note\nread note\nwest\nsouth\neast\ntake brass key\nwest\nwest\nnorth\nlook\ninventory\ndrop note\ninventory\nquit\n' | ./gbasic examples/adventure/adventure.bas >"$stdout_file" 2>"$stderr_file"; then
    actual_text="$(cat "$stdout_file")"
    expected_text="$(cat examples/adventure/adventure.out)"
    if [[ "$actual_text" == "$expected_text" ]]; then
        printf 'PASS %s\n' "examples/adventure/adventure.bas"
    else
        printf 'FAIL %s\n' "examples/adventure/adventure.bas"
        actual_norm="$(mktemp)"
        expected_norm="$(mktemp)"
        printf '%s\n' "$actual_text" >"$actual_norm"
        printf '%s\n' "$expected_text" >"$expected_norm"
        diff -u "$expected_norm" "$actual_norm" || true
        rm -f "$actual_norm" "$expected_norm" "$stdout_file" "$stderr_file"
        exit 1
    fi
else
    status=$?
    printf 'FAIL %s\n' "examples/adventure/adventure.bas"
    if [[ -s "$stderr_file" ]]; then
        cat "$stderr_file"
    fi
    rm -f "$stdout_file" "$stderr_file"
    exit "$status"
fi
rm -f "$stdout_file" "$stderr_file"

stdout_file="$(mktemp)"
stderr_file="$(mktemp)"
if ./gbasic --add-loads examples/add_uses_test.bas >"$stdout_file" 2>"$stderr_file"; then
    actual_text="$(cat "$stdout_file")"
    expected_text="$(cat examples/add_loads_test.out)"
    if [[ "$actual_text" == "$expected_text" ]]; then
        printf 'PASS %s\n' "examples/add_uses_test.bas --add-loads"
    else
        printf 'FAIL %s\n' "examples/add_uses_test.bas --add-loads"
        actual_norm="$(mktemp)"
        expected_norm="$(mktemp)"
        printf '%s\n' "$actual_text" >"$actual_norm"
        printf '%s\n' "$expected_text" >"$expected_norm"
        diff -u "$expected_norm" "$actual_norm" || true
        rm -f "$actual_norm" "$expected_norm" "$stdout_file" "$stderr_file"
        exit 1
    fi
else
    status=$?
    printf 'FAIL %s\n' "examples/add_uses_test.bas --add-loads"
    if [[ -s "$stderr_file" ]]; then
        cat "$stderr_file"
    fi
    rm -f "$stdout_file" "$stderr_file"
    exit "$status"
fi
rm -f "$stdout_file" "$stderr_file"

stdout_file="$(mktemp)"
stderr_file="$(mktemp)"
if ./gbasic --add-uses examples/add_uses_test.bas >"$stdout_file" 2>"$stderr_file"; then
    actual_text="$(cat "$stdout_file")"
    expected_text="$(cat examples/add_uses_insert_test.out)"
    if [[ "$actual_text" == "$expected_text" ]]; then
        printf 'PASS %s\n' "examples/add_uses_test.bas --add-uses"
    else
        printf 'FAIL %s\n' "examples/add_uses_test.bas --add-uses"
        actual_norm="$(mktemp)"
        expected_norm="$(mktemp)"
        printf '%s\n' "$actual_text" >"$actual_norm"
        printf '%s\n' "$expected_text" >"$expected_norm"
        diff -u "$expected_norm" "$actual_norm" || true
        rm -f "$actual_norm" "$expected_norm" "$stdout_file" "$stderr_file"
        exit 1
    fi
else
    status=$?
    printf 'FAIL %s\n' "examples/add_uses_test.bas --add-uses"
    if [[ -s "$stderr_file" ]]; then
        cat "$stderr_file"
    fi
    rm -f "$stdout_file" "$stderr_file"
    exit "$status"
fi
rm -f "$stdout_file" "$stderr_file"

stdout_file="$(mktemp)"
stderr_file="$(mktemp)"
if ./gbasic --add-uses examples/add_uses_builtins_test.bas >"$stdout_file" 2>"$stderr_file"; then
    actual_text="$(cat "$stdout_file")"
    expected_text="$(cat examples/add_uses_builtins_test.out)"
    if [[ "$actual_text" == "$expected_text" ]]; then
        printf 'PASS %s\n' "examples/add_uses_builtins_test.bas --add-uses"
    else
        printf 'FAIL %s\n' "examples/add_uses_builtins_test.bas --add-uses"
        actual_norm="$(mktemp)"
        expected_norm="$(mktemp)"
        printf '%s\n' "$actual_text" >"$actual_norm"
        printf '%s\n' "$expected_text" >"$expected_norm"
        diff -u "$expected_norm" "$actual_norm" || true
        rm -f "$actual_norm" "$expected_norm" "$stdout_file" "$stderr_file"
        exit 1
    fi
else
    status=$?
    printf 'FAIL %s\n' "examples/add_uses_builtins_test.bas --add-uses"
    if [[ -s "$stderr_file" ]]; then
        cat "$stderr_file"
    fi
    rm -f "$stdout_file" "$stderr_file"
    exit "$status"
fi
rm -f "$stdout_file" "$stderr_file"
