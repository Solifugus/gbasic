#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")/.."

# edgar negative tests create a throwaway sqlite cache before raising; clean it.
cleanup_negative_artifacts() {
    rm -f examples/tmp_edgar_neg_identity.db examples/tmp_edgar_neg_miss.db examples/tmp_edgar_neg_doc.db examples/tmp_xml_uac.xml examples/tmp_xml_deep.xml examples/tmp_xml_sub.xml
}
trap cleanup_negative_artifacts EXIT

cases=(
    negative_append_type
    negative_insert_type
    negative_insert_bounds
    negative_remove_bounds
    negative_insert_fractional
    negative_take_first_type
    negative_take_first_empty
    negative_take_last_empty
    negative_reverse_type
    negative_unique_type
    negative_encode_typed
    negative_compare_array_order
    negative_compare_record_order
    negative_compare_array_scalar_order
    negative_compare_record_scalar_order
    negative_unique_nested_array
    negative_sort_type
    negative_sort_mixed
    negative_sort_nested_array
    negative_invalid_escape
    negative_add_bool
    negative_add_nothing
    negative_add_array
    negative_add_record
    negative_type_builtin_arity
    negative_string_arithmetic
    negative_unary_string_arithmetic
    negative_unterminated_string
    negative_multiline_unterminated_string
    negative_multiline_string_line_tracking
    negative_datetime_string_comparison
    negative_now_arity
    negative_unknown_order
    negative_env_arity
    negative_env_type
    negative_sleep_arity
    negative_sleep_type
    negative_sleep_negative
    negative_edgar_unset_identify
    negative_edgar_offline_miss
    negative_edgar_document_miss
    negative_xml_not_loaded
    negative_xml_malformed
    negative_xml_parse_arity
    negative_xml_parse_type
    negative_xml_parse_html_arity
    negative_xml_parse_html_type
    negative_xml_find_arity
    negative_xml_find_path_type
    negative_xml_attr_arity
    negative_xml_encode_arity
    negative_xml_encode_malformed
    negative_xml_read_type
    negative_xml_read_closed
    negative_xml_reader_missing
    negative_xml_reader_depth
    negative_xml_subtree_non_element
    negative_xml_subtree_closed
    negative_xml_skip_to_type
    negative_xml_skip_to_closed
    negative_ownership_stake_unstructured
    negative_llm_retry_exhausted
    negative_llm_offline_miss
    negative_llm_tool_loop_limit
    negative_llm_tool_duplicate
    negative_llm_tool_not_callable
    negative_json_strict_unknown
    negative_json_strict_nonfinite
    negative_json_strict_live
    negative_reflect_unknown_var
    negative_reflect_count_scalar
    negative_reflect_unknown_field
    negative_reflect_element_oor
    negative_reflect_bad_function
    negative_chained_lvalue
    negative_chained_not_callable
    negative_chained_method_missing
    negative_password_hash_arity
    negative_password_hash_type
    negative_password_verify_arity
    negative_password_verify_password_type
    negative_password_verify_hash_type
    negative_secure_token_arity
    negative_secure_token_type
    negative_secure_token_fractional
    negative_secure_token_zero
    negative_secure_token_large
    negative_sqlite_not_loaded
    negative_pg_not_loaded
    negative_pg_connect_type
    negative_pg_connect_field
    negative_pg_close_arity
    negative_pg_close_type
    negative_pg_query_arity
    negative_pg_query_connection
    negative_pg_exec_arity
    negative_pg_begin_arity
    negative_pg_commit_arity
    negative_pg_rollback_arity
    negative_pg_transaction_type
    negative_webclient_get_url_type
    negative_webclient_post_url_type
    negative_webclient_post_body_type
    negative_webclient_request_type
    negative_webclient_request_missing_url
    negative_webclient_request_method_type
    negative_webclient_request_headers_type
    negative_webclient_request_header_value
    negative_webclient_request_body_type
    negative_webclient_malformed_url
    negative_webserver_listen_port
    negative_webserver_response_missing_id
    negative_webserver_response_body_type
    negative_webserver_response_headers_type
    negative_webserver_response_header_value
    negative_webserver_response_cookies_type
    negative_webserver_response_cookie_value
    negative_webserver_response_cookie_newline
    negative_webserver_redirect_arity
    negative_webserver_redirect_request_type
    negative_webserver_redirect_request_id
    negative_webserver_redirect_location_type
    negative_webserver_redirect_location_invalid
    negative_webserver_redirect_status
    negative_find_type
    negative_function_assignment
    negative_function_compare_order
    negative_this_outside_method
    negative_this_read_only
    negative_method_attach_non_record
    negative_constructor_params
    negative_len_assignment
    negative_foo_assignment
    negative_getname_assignment
    negative_function_result_modifier
    negative_call_lvalue_index
    negative_call_lvalue_field
    negative_call_lvalue_dynamic_index
    negative_temporary_lvalue
    negative_malformed_lvalue
    negative_array_string_key
    negative_record_numeric_key
    negative_noncontainer_dynamic_lookup
    negative_consider_missing_end
    negative_consider_else_before_if
    negative_consider_duplicate_else
    negative_consider_malformed
    negative_inline_if_block_else_missing_end
    negative_inline_if_compound_statement
    negative_while_missing_end
    negative_break_outside_loop
    negative_continue_outside_loop
    negative_goto_top_level
    negative_gosub_top_level
    negative_for_each_number
    negative_for_each_record
    negative_file_size_arity
    negative_file_size_type
    negative_file_size_directory
    negative_file_size_missing
    negative_file_mtime_arity
    negative_file_mtime_type
    negative_file_mtime_missing
    negative_atomic_replace_arity
    negative_atomic_replace_type
    negative_atomic_replace_missing
    negative_file_delete_type
    negative_file_copy_type
    negative_file_move_type
    negative_file_list_type
    negative_file_copy_missing
    negative_file_move_missing
    negative_file_copy_target
    negative_file_move_target
    negative_make_dir_type
    negative_remove_dir_type
    negative_remove_dir_missing
    negative_remove_dir_nonempty
    negative_make_dir_exists
    negative_overwrite_file_type
    negative_overwrite_text_type
    negative_overwrite_negative_position
    negative_overwrite_fractional_position
    negative_overwrite_beyond_eof
    negative_overwrite_missing
    negative_path_join_type
    negative_path_unary_type
    negative_path_join_missing
    negative_path_unary_missing
    negative_path_join_too_many
    negative_path_unary_too_many
    negative_read_lines_type
    negative_read_lines_missing
    negative_read_lines_no_arguments
    negative_read_lines_too_many
    negative_left_type
    negative_mid_arity
    negative_trim_type
    negative_split_empty_separator
    negative_join_type
    negative_join_element_type
    negative_modifier_split_empty
    negative_modifier_join_element_type
    negative_lowered_modifier_type
    negative_uppered_modifier_type
    negative_number_modifier_invalid
    negative_number_time_only
    negative_bitwise_noninteger
    negative_bitwise_range
    negative_bitwise_negative
    negative_bitwise_shift_count
    negative_bitwise_arity
    negative_decode_malformed
    negative_quote_record
    negative_gui_duplicate_id
    negative_gui_missing_id
    negative_gui_unknown_component
    negative_gui_invalid_spacing
    negative_gui_reserved_id
    negative_gui_invalid_id
    negative_watcher_cycle
    negative_chr_arity
    negative_chr_type
    negative_chr_range
    negative_chr_surrogate
    negative_chr_fractional
    negative_code_arity
    negative_code_type
    negative_code_empty
    negative_uesc_surrogate
    negative_uesc_range
    negative_uesc_zero
    negative_deserialize_type
    negative_deserialize_garbage
    negative_serialize_arity
    negative_send_non_actor
    negative_self_args
    negative_send_arity
    negative_send_strict_link
    negative_send_strict_type
    negative_spawn_unknown
    negative_spawn_arity
    negative_receive_arity
    negative_receive_timeout_type
    negative_pbi_unknown_policy
    negative_pbi_reset_missing_value
    negative_pbi_value_on_nonreset
    negative_pbi_new_non_record
    negative_seed_arity
    negative_seed_type
    negative_random_arity
    negative_random_int_arity
    negative_random_int_order
    negative_random_int_fractional
    # Rebaselined + wired in D0.5 (previously orphaned with the retired 3-line
    # "Error code: N" format; re-captured to the current single-line format).
    negative_count_nothing
    negative_count_unknown
    negative_count_number
    negative_count_boolean_true
    negative_count_boolean_false
    negative_count_arity_zero
    negative_count_arity_two
    negative_repeat_type
    negative_repeat_negative
    negative_repeat_fractional
    negative_replace_type
    negative_replace_empty_search
    negative_starts_with_type
    negative_ends_with_type
    negative_has_key_type
    negative_has_record_type
    negative_keys_type
    negative_values_type
    negative_remove_key_record_type
    negative_remove_key_key_type
    negative_number_conversion
    negative_boolean_conversion
    negative_array_conversion
    negative_record_conversion
    negative_print_to_missing_expr
    negative_print_to_unknown_stream
    negative_try_decode_arity
    negative_monotonic_arity
    negative_modifier_escape_invalid
    negative_modifier_escape_surrogate
    negative_modifier_escape_nul
    negative_clause_residual
    negative_clause_stmt_start
    negative_try_decode_type
    negative_library_error_path
    negative_for_step_zero
    negative_for_bound_type
)

for name in "${cases[@]}"; do
    source="tests/$name.bas"
    expected="tests/$name.err"
    stdout_file="$(mktemp)"
    stderr_file="$(mktemp)"
    run_prefix=()
    if [[ "$name" == negative_gui_* ]]; then
        run_prefix=(env GBASIC_PATH=stdlib)
    fi

    if "${run_prefix[@]}" ./gbasic "$source" >"$stdout_file" 2>"$stderr_file"; then
        printf 'FAIL %s\n' "$source"
        printf 'expected nonzero exit\n'
        rm -f "$stdout_file" "$stderr_file"
        exit 1
    fi

    actual_text="$(cat "$stderr_file")"
    expected_text="$(cat "$expected")"

    # A few goldens pin a message that libxml2 WROTE, not one we wrote, and
    # libxml2 rewords its diagnostics between releases. negative_xml_reader_depth
    # is the example: 2.15.2 says "Excessive depth in document: 256, use
    # XML_PARSE_HUGE option ... column 774" and 2.9.14 (Ubuntu 24.04 LTS, and the
    # riscv64 VM) drops the comma and reports column 777. Unlike
    # negative_xml_malformed -- fixed by choosing an input whose message is
    # stable -- there is no wording here that both versions produce, because the
    # COLUMN differs too.
    #
    # So such a case declares the libxml2 it was captured against in a sidecar
    # `tests/NAME.libxml2min`, and is skipped on anything older. A sidecar rather
    # than a name hardcoded in this runner: the requirement then lives next to
    # the golden it constrains, and shows up when someone looks at the case.
    minfile="tests/$name.libxml2min"
    if [ -f "$minfile" ] && command -v pkg-config >/dev/null 2>&1; then
        have="$(pkg-config --modversion libxml-2.0 2>/dev/null || echo "")"
        want="$(tr -d '[:space:]' <"$minfile")"
        if [ -n "$have" ] && [ "$have" != "$want" ] &&
           [ "$(printf '%s\n%s\n' "$have" "$want" | sort -V | head -1)" = "$have" ]; then
            printf 'SKIP %s (golden pins libxml2 %s wording; this build has %s)\n' \
                   "$source" "$want" "$have"
            rm -f "$stdout_file" "$stderr_file"
            continue
        fi
    fi
    # Same optional-dependency rule as run_examples.sh: when a module is compiled
    # out, its negative cases raise the build's "not available" error instead of
    # the specific misuse they pin, which is correct behaviour and not a failure.
    # Only skip when the EXPECTED text is not itself that message -- negative_
    # xml_not_loaded and friends deliberately assert an unavailable module, and
    # must still be compared rather than skipped past.
    if [[ "$actual_text" != "$expected_text" ]] &&
       grep -qE 'support is (not available in this build|unavailable)' "$stderr_file" &&
       ! grep -qE 'support is (not available in this build|unavailable)' "$expected"; then
        printf 'SKIP %s (module compiled out)\n' "$source"
        rm -f "$stdout_file" "$stderr_file"
        continue
    fi
    if [[ "$actual_text" == "$expected_text" ]]; then
        printf 'PASS %s\n' "$source"
    else
        printf 'FAIL %s\n' "$source"
        actual_norm="$(mktemp)"
        expected_norm="$(mktemp)"
        printf '%s\n' "$actual_text" >"$actual_norm"
        printf '%s\n' "$expected_text" >"$expected_norm"
        diff -u "$expected_norm" "$actual_norm" || true
        rm -f "$actual_norm" "$expected_norm" "$stdout_file" "$stderr_file"
        exit 1
    fi

    if [[ -s "$stdout_file" ]]; then
        printf 'FAIL %s\n' "$source"
        printf 'expected empty stdout\n'
        cat "$stdout_file"
        rm -f "$stdout_file" "$stderr_file"
        exit 1
    fi

    rm -f "$stdout_file" "$stderr_file"
done
