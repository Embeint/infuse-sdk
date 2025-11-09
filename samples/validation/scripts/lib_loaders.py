#!/usr/bin/env python3

import openhtf
from lib_config import SubtestConfig, ValidationConfig
from lib_exceptions import ConfigurationError
from lib_phases import (
    flash_binaries,
    get_device_id,
    nrf91_modem_program,
    operator_ack_result,
    provision_dut,
    recover_dut,
    validate_dut,
)
from openhtf.core.measurements import Measurement, Outcome


def load_recovery_phase(config: ValidationConfig) -> list[openhtf.PhaseDescriptor]:
    # Run the recover phase if any SoCs need it
    if any(soc.recover for soc in config.socs.values()):
        return [recover_dut.with_args(config=config)]
    return []


def load_provisioning_phase(config: ValidationConfig) -> list[openhtf.PhaseDescriptor]:
    # Provision board in Infuse-IoT if any SoC phase requests it
    if config.infuse_provisions:
        return [provision_dut.with_args(config=config)]
    return []


def load_nrf91_modem_phase(config: ValidationConfig) -> list[openhtf.PhaseDescriptor]:
    # Run the nRF91 modem programming phase if any SoCs need it
    modems = [soc for soc in config.socs.values() if soc.nrf91_modem_firmware]
    if len(modems) > 1:
        raise ConfigurationError("More than one nRF91 modem?")

    if len(modems) == 1:
        return [nrf91_modem_program.with_args(config=config)]
    return []


def load_flash_phase(config: ValidationConfig, phase: str) -> list[openhtf.PhaseDescriptor]:
    phase_imgs = [x for x in config.socs.values() if phase in x.binaries]

    if len(phase_imgs) > 0:
        return [flash_binaries.with_args(config=config, phase=phase)]
    return []


def _load_measurement(name: str, subtest: SubtestConfig) -> Measurement:
    meas = openhtf.Measurement(name)
    if equals := subtest.equals():
        meas = meas.equals(equals)
    if in_range := subtest.in_range():
        meas = meas.in_range(in_range[0], in_range[1])
    if unit := subtest.unit:
        try:
            meas = meas.with_units(unit)
        except KeyError as e:
            err = f"Unit '{unit}' for {name} does not exist"
            raise ConfigurationError(err) from e
    return meas


def load_subsystem_tests(config: ValidationConfig) -> list[openhtf.PhaseDescriptor]:
    subsystems = []

    for test_name, test_config in config.tests.items():
        subsystems.append(openhtf.Measurement(test_name).equals(Outcome.PASS.value))
        for subtest_name, subtest_config in test_config.subtests.items():
            val_name = f"{test_name}.{subtest_name}"
            subsystems.append(_load_measurement(val_name, subtest_config))

    return [openhtf.measures(*subsystems)(validate_dut.with_args(config=config))]


def load_default_tests(config: ValidationConfig) -> openhtf.Test:
    phase_group = []
    phase_group += load_recovery_phase(config)
    phase_group += [get_device_id.with_args(config=config)]
    phase_group += load_provisioning_phase(config)
    phase_group += load_nrf91_modem_phase(config)
    phase_group += load_flash_phase(config, "test")
    phase_group += load_subsystem_tests(config)
    phase_group += load_flash_phase(config, "success")

    test = openhtf.Test(
        openhtf.PhaseGroup.with_teardown(operator_ack_result)(
            *phase_group,
        )
    )
    return test
