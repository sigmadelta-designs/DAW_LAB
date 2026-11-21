#include "DdrControllerEditor.h"

DdrControllerEditor::DdrControllerEditor (DdrController& controllerToEdit)
    : DdrSimpleParamEditor (controllerToEdit, 460)
{
    using D = ControlTip::Domain;

    addCombo ("targetchip", "Target chip", { "Chip 0", "Chip 1", "Chip 2", "Chip 3" }, D::dut,
        "Which CS wire this controller asserts for every command - the same kind of choice a real memory "
        "controller's chip-select/rank-select register makes. A chip only ever answers while its own CS is "
        "asserted, so this picks which DdrChip on the bus the BIST actually talks to.");
    addSlider ("caslatency", "CAS latency (UI)", D::dut,
        "The minimum wait, in UI, before this controller even checks for a read's reply - the same real "
        "DRAM timing parameter (tCL) a chip's mode register programs, just in this bus's own UI units "
        "rather than real nanoseconds. Set it shorter than a chip actually needs and reads come back late "
        "or not at all, the same bring-up symptom an untrained real CAS latency produces.");

    addHeading ("Built-in self-test (BIST)");
    addCombo ("pattern", "BIST pattern", { "All zero", "All one", "Address as data", "Checkerboard" }, D::simulation,
        "Which data pattern the self-test writes and verifies - a stimulus choice for this test run, the "
        "same as picking a pattern on a real memory BERT, not a register on the DUT.");
    addSlider ("burstwords", "Sweep length (words)", D::simulation,
        "How many words the self-test's write/verify sweep walks each pass - a test-bench setting for how "
        "much of the address space gets exercised, not a real DRAM timing property.");
    addToggle ("running", "BIST running", D::simulation,
        "Starts or stops the self-test loop. Off just holds the bus idle so you can look at a snapshot of "
        "whatever state it's already in, without a new write/read sweep disturbing it.");

    finishLayout();
}
