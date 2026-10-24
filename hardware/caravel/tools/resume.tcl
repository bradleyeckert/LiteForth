# usage: flow.tcl -interactive -file resume.tcl   (env: RESUME_DESIGN, RESUME_TAG, RESUME_FROM)
package require openlane
source /home/claude/tools/flow_steps.tcl
set d $::env(RESUME_DESIGN); set t $::env(RESUME_TAG)
set cfg $d/runs/$t/config.tcl
file rename -force $cfg $cfg.saved
prep -design $d -tag $t -ignore_mismatches
source $cfg.saved
set steps [list \
    irdrop run_irdrop_report_step \
    gds_magic run_magic_step \
    gds_klayout run_klayout_step \
    lvs run_lvs_step \
    drc run_drc_step \
    antenna_check run_antenna_check_step \
    cvc_rv run_erc_step ]
set go 0
foreach {name exe} $steps {
    if { $name == $::env(RESUME_FROM) } { set go 1 }
    if { $go } {
        set ::env(CURRENT_STEP) $name
        if { [catch $exe err] } { puts_err "Step $name failed: $err"; flow_fail }
    }
}
save_final_views
calc_total_runtime
save_state
generate_final_summary_report
if { [catch run_timing_check_step] } { puts_err "timing check failed" }
puts_success "Flow complete."
