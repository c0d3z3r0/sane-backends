/* sane - Scanner Access Now Easy.

   Copyright (C) 2003, 2004 Henning Meier-Geinitz <henning@meier-geinitz.de>
   Copyright (C) 2004, 2005 Gerhard Jaeger <gerhard@gjaeger.de>
   Copyright (C) 2004-2016 Stéphane Voltz <stef.dev@free.fr>
   Copyright (C) 2005-2009 Pierre Willenbrock <pierre@pirsoft.dnsalias.org>
   Copyright (C) 2006 Laurent Charpentier <laurent_pubs@yahoo.com>
   Copyright (C) 2007 Luke <iceyfor@gmail.com>
   Copyright (C) 2010 Chris Berry <s0457957@sms.ed.ac.uk> and Michael Rickmann <mrickma@gwdg.de>
                 for Plustek Opticbook 3600 support

   Dynamic rasterization code was taken from the epjistsu backend by
   m. allan noah <kitno455 at gmail dot com>

   Software processing for deskew, crop and dspeckle are inspired by allan's
   noah work in the fujitsu backend

   This file is part of the SANE package.

   This program is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License as
   published by the Free Software Foundation; either version 2 of the
   License, or (at your option) any later version.

   This program is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place - Suite 330, Boston,
   MA 02111-1307, USA.

   As a special exception, the authors of SANE give permission for
   additional uses of the libraries contained in this release of SANE.

   The exception is that, if you link a SANE library with other files
   to produce an executable, this does not by itself cause the
   resulting executable to be covered by the GNU General Public
   License.  Your use of that executable is in no way restricted on
   account of linking the SANE library code into it.

   This exception does not, however, invalidate any other reasons why
   the executable file might be covered by the GNU General Public
   License.

   If you submit changes to SANE to the maintainers to be included in
   a subsequent release, you agree by submitting the changes that
   those changes may be distributed with this exception intact.

   If you write modifications of your own for SANE, it is your choice
   whether to permit this exception to apply to your modifications.
   If you do not wish that, delete this exception notice.
*/

/*
 * SANE backend for Genesys Logic GL646/GL841/GL842/GL843/GL846/GL847/GL124 based scanners
 */

#define DEBUG_NOT_STATIC

#include "genesys.h"
#include "conv.h"
#include "gl124_registers.h"
#include "gl841_registers.h"
#include "gl843_registers.h"
#include "gl846_registers.h"
#include "gl847_registers.h"
#include "usb_device.h"
#include "utilities.h"
#include "scanner_interface_usb.h"
#include "test_scanner_interface.h"
#include "test_settings.h"
#include "../include/sane/sanei_config.h"
#include "../include/sane/sanei_magic.h"

#include <array>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iterator>
#include <list>
#include <numeric>
#include <exception>
#include <vector>

#ifndef SANE_GENESYS_API_LINKAGE
#define SANE_GENESYS_API_LINKAGE extern "C"
#endif

namespace genesys {

// Data that we allocate to back SANE_Device objects in s_sane_devices
struct SANE_Device_Data
{
    std::string name;
};

namespace {
    StaticInit<std::list<Genesys_Scanner>> s_scanners;
    StaticInit<std::vector<SANE_Device>> s_sane_devices;
    StaticInit<std::vector<SANE_Device_Data>> s_sane_devices_data;
    StaticInit<std::vector<SANE_Device*>> s_sane_devices_ptrs;
    StaticInit<std::list<Genesys_Device>> s_devices;

    // Maximum time for lamp warm-up
    constexpr unsigned WARMUP_TIME = 65;
} // namespace

static SANE_String_Const mode_list[] = {
  SANE_VALUE_SCAN_MODE_COLOR,
  SANE_VALUE_SCAN_MODE_GRAY,
  /* SANE_TITLE_HALFTONE,  currently unused */
  SANE_VALUE_SCAN_MODE_LINEART,
    nullptr
};

static SANE_String_Const color_filter_list[] = {
  SANE_I18N ("Red"),
  SANE_I18N ("Green"),
  SANE_I18N ("Blue"),
    nullptr
};

static SANE_String_Const cis_color_filter_list[] = {
  SANE_I18N ("Red"),
  SANE_I18N ("Green"),
  SANE_I18N ("Blue"),
  SANE_I18N ("None"),
    nullptr
};

static SANE_Range swdespeck_range = {
  1,
  9,
  1
};

static SANE_Range time_range = {
  0,				/* minimum */
  60,				/* maximum */
  0				/* quantization */
};

static const SANE_Range u12_range = {
  0,				/* minimum */
  4095,				/* maximum */
  0				/* quantization */
};

static const SANE_Range u14_range = {
  0,				/* minimum */
  16383,			/* maximum */
  0				/* quantization */
};

static const SANE_Range u16_range = {
  0,				/* minimum */
  65535,			/* maximum */
  0				/* quantization */
};

static const SANE_Range percentage_range = {
    float_to_fixed(0),     // minimum
    float_to_fixed(100),   // maximum
    float_to_fixed(1)      // quantization
};

static const SANE_Range threshold_curve_range = {
  0,			/* minimum */
  127,		        /* maximum */
  1			/* quantization */
};

/**
 * range for brightness and contrast
 */
static const SANE_Range enhance_range = {
  -100,	/* minimum */
  100,		/* maximum */
  1		/* quantization */
};

/**
 * range for expiration time
 */
static const SANE_Range expiration_range = {
  -1,	        /* minimum */
  30000,	/* maximum */
  1		/* quantization */
};

const Genesys_Sensor& sanei_genesys_find_sensor_any(Genesys_Device* dev)
{
    DBG_HELPER(dbg);
    for (const auto& sensor : *s_sensors) {
        if (dev->model->sensor_id == sensor.sensor_id) {
            return sensor;
        }
    }
    throw std::runtime_error("Given device does not have sensor defined");
}

Genesys_Sensor* find_sensor_impl(Genesys_Device* dev, unsigned dpi, unsigned channels,
                                 ScanMethod scan_method)
{
    DBG_HELPER_ARGS(dbg, "dpi: %d, channels: %d, scan_method: %d", dpi, channels,
                    static_cast<unsigned>(scan_method));
    for (auto& sensor : *s_sensors) {
        if (dev->model->sensor_id == sensor.sensor_id && sensor.resolutions.matches(dpi) &&
            sensor.matches_channel_count(channels) && sensor.method == scan_method)
        {
            return &sensor;
        }
    }
    return nullptr;
}

bool sanei_genesys_has_sensor(Genesys_Device* dev, unsigned dpi, unsigned channels,
                              ScanMethod scan_method)
{
    DBG_HELPER_ARGS(dbg, "dpi: %d, channels: %d, scan_method: %d", dpi, channels,
                    static_cast<unsigned>(scan_method));
    return find_sensor_impl(dev, dpi, channels, scan_method) != nullptr;
}

const Genesys_Sensor& sanei_genesys_find_sensor(Genesys_Device* dev, unsigned dpi, unsigned channels,
                                                ScanMethod scan_method)
{
    DBG_HELPER_ARGS(dbg, "dpi: %d, channels: %d, scan_method: %d", dpi, channels,
                    static_cast<unsigned>(scan_method));
    const auto* sensor = find_sensor_impl(dev, dpi, channels, scan_method);
    if (sensor)
        return *sensor;
    throw std::runtime_error("Given device does not have sensor defined");
}

Genesys_Sensor& sanei_genesys_find_sensor_for_write(Genesys_Device* dev, unsigned dpi,
                                                    unsigned channels,
                                                    ScanMethod scan_method)
{
    DBG_HELPER_ARGS(dbg, "dpi: %d, channels: %d, scan_method: %d", dpi, channels,
                    static_cast<unsigned>(scan_method));
    auto* sensor = find_sensor_impl(dev, dpi, channels, scan_method);
    if (sensor)
        return *sensor;
    throw std::runtime_error("Given device does not have sensor defined");
}


std::vector<std::reference_wrapper<const Genesys_Sensor>>
    sanei_genesys_find_sensors_all(Genesys_Device* dev, ScanMethod scan_method)
{
    DBG_HELPER_ARGS(dbg, "scan_method: %d", static_cast<unsigned>(scan_method));
    std::vector<std::reference_wrapper<const Genesys_Sensor>> ret;
    for (const Genesys_Sensor& sensor : sanei_genesys_find_sensors_all_for_write(dev, scan_method)) {
        ret.push_back(sensor);
    }
    return ret;
}

std::vector<std::reference_wrapper<Genesys_Sensor>>
    sanei_genesys_find_sensors_all_for_write(Genesys_Device* dev, ScanMethod scan_method)
{
    DBG_HELPER_ARGS(dbg, "scan_method: %d", static_cast<unsigned>(scan_method));
    std::vector<std::reference_wrapper<Genesys_Sensor>> ret;
    for (auto& sensor : *s_sensors) {
        if (dev->model->sensor_id == sensor.sensor_id && sensor.method == scan_method) {
            ret.push_back(sensor);
        }
    }
    return ret;
}

void sanei_genesys_init_structs (Genesys_Device * dev)
{
    DBG_HELPER(dbg);

    bool gpo_ok = false;
    bool motor_ok = false;
    bool fe_ok = false;

  /* initialize the GPO data stuff */
    for (const auto& gpo : *s_gpo) {
        if (dev->model->gpio_id == gpo.id) {
            dev->gpo = gpo;
            gpo_ok = true;
            break;
        }
    }

    // initialize the motor data stuff
    for (const auto& motor : *s_motors) {
        if (dev->model->motor_id == motor.id) {
            dev->motor = motor;
            motor_ok = true;
            break;
        }
    }

    for (const auto& frontend : *s_frontends) {
        if (dev->model->adc_id == frontend.id) {
            dev->frontend_initial = frontend;
            dev->frontend = frontend;
            fe_ok = true;
            break;
        }
    }

    if (!motor_ok || !gpo_ok || !fe_ok) {
        throw SaneException("bad description(s) for fe/gpo/motor=%d/%d/%d\n",
                            static_cast<unsigned>(dev->model->sensor_id),
                            static_cast<unsigned>(dev->model->gpio_id),
                            static_cast<unsigned>(dev->model->motor_id));
    }
}

/* Generate slope table for motor movement */
/**
 * This function generates a slope table using the slope from the motor struct
 * truncated at the given exposure time or step count, whichever comes first.
 * The summed time of the acceleration steps is returned, and the
 * number of accerelation steps is put into used_steps.
 *
 * @param dev            Device struct
 * @param slope_table    Table to write to
 * @param step_type      Generate table for this step_type. 0=>full, 1=>half,
 *                       2=>quarter
 * @param exposure_time  Minimum exposure time of a scan line
 * @param yres           Resolution of a scan line
 * @param used_steps     Final number of steps is stored here
 * @return               Motor slope table
 * @note  all times in pixel time
 */
MotorSlopeTable sanei_genesys_create_slope_table3(AsicType asic_type, const Genesys_Motor& motor,
                                                  StepType step_type, int exposure_time,
                                                  unsigned yres)
{
    unsigned target_speed_w = (exposure_time * yres) / motor.base_ydpi;

    return create_slope_table(motor.get_slope_with_step_type(step_type), target_speed_w,
                              step_type, 1, 1, get_slope_table_max_size(asic_type));
}

/** @brief computes gamma table
 * Generates a gamma table of the given length within 0 and the given
 * maximum value
 * @param gamma_table gamma table to fill
 * @param size size of the table
 * @param maximum value allowed for gamma
 * @param gamma_max maximum gamma value
 * @param gamma gamma to compute values
 * @return a gamma table filled with the computed values
 * */
void
sanei_genesys_create_gamma_table (std::vector<uint16_t>& gamma_table, int size,
                                  float maximum, float gamma_max, float gamma)
{
    gamma_table.clear();
    gamma_table.resize(size, 0);

  int i;
  float value;

  DBG(DBG_proc, "%s: size = %d, ""maximum = %g, gamma_max = %g, gamma = %g\n", __func__, size,
      maximum, gamma_max, gamma);
  for (i = 0; i < size; i++)
    {
        value = static_cast<float>(gamma_max * std::pow(static_cast<double>(i) / size, 1.0 / gamma));
        if (value > maximum) {
            value = maximum;
        }
        gamma_table[i] = static_cast<std::uint16_t>(value);
    }
  DBG(DBG_proc, "%s: completed\n", __func__);
}

void sanei_genesys_create_default_gamma_table(Genesys_Device* dev,
                                              std::vector<uint16_t>& gamma_table, float gamma)
{
    int size = 0;
    int max = 0;
    if (dev->model->asic_type == AsicType::GL646) {
        if (has_flag(dev->model->flags, ModelFlag::GAMMA_14BIT)) {
            size = 16384;
        } else {
            size = 4096;
        }
        max = size - 1;
    } else if (dev->model->asic_type == AsicType::GL124 ||
               dev->model->asic_type == AsicType::GL846 ||
               dev->model->asic_type == AsicType::GL847) {
        size = 257;
        max = 65535;
    } else {
        size = 256;
        max = 65535;
    }
    sanei_genesys_create_gamma_table(gamma_table, size, max, max, gamma);
}

/* computes the exposure_time on the basis of the given vertical dpi,
   the number of pixels the ccd needs to send,
   the step_type and the corresponding maximum speed from the motor struct */
/*
  Currently considers maximum motor speed at given step_type, minimum
  line exposure needed for conversion and led exposure time.

  TODO: Should also consider maximum transfer rate: ~6.5MB/s.
    Note: The enhance option of the scanners does _not_ help. It only halves
          the amount of pixels transfered.
 */
SANE_Int sanei_genesys_exposure_time2(Genesys_Device * dev, float ydpi,
                                      StepType step_type, int endpixel, int exposure_by_led)
{
  int exposure_by_ccd = endpixel + 32;
    unsigned max_speed_motor_w = dev->motor.get_slope_with_step_type(step_type).max_speed_w;
    int exposure_by_motor = static_cast<int>((max_speed_motor_w * dev->motor.base_ydpi) / ydpi);

  int exposure = exposure_by_ccd;

  if (exposure < exposure_by_motor)
    exposure = exposure_by_motor;

  if (exposure < exposure_by_led && dev->model->is_cis)
    exposure = exposure_by_led;

    DBG(DBG_info, "%s: ydpi=%d, step=%d, endpixel=%d led=%d => exposure=%d\n", __func__,
        static_cast<int>(ydpi), static_cast<unsigned>(step_type), endpixel,
        exposure_by_led, exposure);
  return exposure;
}


/* Sends a block of shading information to the scanner.
   The data is placed at address 0x0000 for color mode, gray mode and
   unconditionally for the following CCD chips: HP2300, HP2400 and HP5345
   In the other cases (lineart, halftone on ccd chips not mentioned) the
   addresses are 0x2a00 for dpihw==0, 0x5500 for dpihw==1 and 0xa800 for
   dpihw==2. //Note: why this?

   The data needs to be of size "size", and in little endian byte order.
 */
static void genesys_send_offset_and_shading(Genesys_Device* dev, const Genesys_Sensor& sensor,
                                            uint8_t* data, int size)
{
    DBG_HELPER_ARGS(dbg, "(size = %d)", size);
  int dpihw;
  int start_address;

  /* ASIC higher than gl843 doesn't have register 2A/2B, so we route to
   * a per ASIC shading data loading function if available.
   * It is also used for scanners using SHDAREA */
    if (dev->cmd_set->has_send_shading_data()) {
        dev->cmd_set->send_shading_data(dev, sensor, data, size);
        return;
    }

  /* gl646, gl84[123] case */
  dpihw = dev->reg.get8(0x05) >> 6;

  /* TODO invert the test so only the 2 models behaving like that are
   * tested instead of adding all the others */
  /* many scanners send coefficient for lineart/gray like in color mode */
  if ((dev->settings.scan_mode == ScanColorMode::LINEART ||
       dev->settings.scan_mode == ScanColorMode::HALFTONE)
        && dev->model->sensor_id != SensorId::CCD_PLUSTEK_OPTICBOOK_3800
        && dev->model->sensor_id != SensorId::CCD_KVSS080
        && dev->model->sensor_id != SensorId::CCD_G4050
        && dev->model->sensor_id != SensorId::CCD_HP_4850C
        && dev->model->sensor_id != SensorId::CCD_CANON_4400F
        && dev->model->sensor_id != SensorId::CCD_CANON_8400F
        && dev->model->sensor_id != SensorId::CCD_CANON_8600F
        && dev->model->sensor_id != SensorId::CCD_DSMOBILE600
        && dev->model->sensor_id != SensorId::CCD_XP300
        && dev->model->sensor_id != SensorId::CCD_DP665
        && dev->model->sensor_id != SensorId::CCD_DP685
        && dev->model->sensor_id != SensorId::CIS_CANON_LIDE_80
        && dev->model->sensor_id != SensorId::CCD_ROADWARRIOR
        && dev->model->sensor_id != SensorId::CCD_HP2300
        && dev->model->sensor_id != SensorId::CCD_HP2400
        && dev->model->sensor_id != SensorId::CCD_HP3670
        && dev->model->sensor_id != SensorId::CCD_5345)	/* lineart, halftone */
    {
        if (dpihw == 0) {		/* 600 dpi */
            start_address = 0x02a00;
        } else if (dpihw == 1) {	/* 1200 dpi */
            start_address = 0x05500;
        } else if (dpihw == 2) {	/* 2400 dpi */
            start_address = 0x0a800;
        } else {			/* reserved */
            throw SaneException("unknown dpihw");
        }
    }
    else { // color
        start_address = 0x00;
    }

    dev->interface->write_buffer(0x3c, start_address, data, size);
}

void sanei_genesys_init_shading_data(Genesys_Device* dev, const Genesys_Sensor& sensor,
                                     int pixels_per_line)
{
    DBG_HELPER_ARGS(dbg, "pixels_per_line: %d", pixels_per_line);

    if (dev->cmd_set->has_send_shading_data()) {
        return;
    }

  DBG(DBG_proc, "%s (pixels_per_line = %d)\n", __func__, pixels_per_line);

    unsigned channels = dev->settings.get_channels();

  // 16 bit black, 16 bit white
  std::vector<uint8_t> shading_data(pixels_per_line * 4 * channels, 0);

  uint8_t* shading_data_ptr = shading_data.data();

    for (unsigned i = 0; i < pixels_per_line * channels; i++) {
      *shading_data_ptr++ = 0x00;	/* dark lo */
      *shading_data_ptr++ = 0x00;	/* dark hi */
      *shading_data_ptr++ = 0x00;	/* white lo */
      *shading_data_ptr++ = 0x40;	/* white hi -> 0x4000 */
    }

    genesys_send_offset_and_shading(dev, sensor, shading_data.data(),
                                    pixels_per_line * 4 * channels);
}

namespace gl124 {
    void gl124_setup_scan_gpio(Genesys_Device* dev, int resolution);
} // namespace gl124

void scanner_clear_scan_and_feed_counts(Genesys_Device& dev)
{
    switch (dev.model->asic_type) {
        case AsicType::GL841: {
            dev.interface->write_register(gl841::REG_0x0D,
                                          gl841::REG_0x0D_CLRLNCNT);
            break;
        }
        case AsicType::GL843: {
            dev.interface->write_register(gl843::REG_0x0D,
                                          gl843::REG_0x0D_CLRLNCNT | gl843::REG_0x0D_CLRMCNT);
            break;
        }
        case AsicType::GL845:
        case AsicType::GL846: {
            dev.interface->write_register(gl846::REG_0x0D,
                                          gl846::REG_0x0D_CLRLNCNT | gl846::REG_0x0D_CLRMCNT);
            break;
        }
        case AsicType::GL847:{
            dev.interface->write_register(gl847::REG_0x0D,
                                          gl847::REG_0x0D_CLRLNCNT | gl847::REG_0x0D_CLRMCNT);
            break;
        }
        case AsicType::GL124:{
            dev.interface->write_register(gl124::REG_0x0D,
                                          gl124::REG_0x0D_CLRLNCNT | gl124::REG_0x0D_CLRMCNT);
            break;
        }
        default:
            throw SaneException("Unsupported asic type");
    }
}

bool scanner_is_motor_stopped(Genesys_Device& dev)
{
    switch (dev.model->asic_type) {
        case AsicType::GL646: {
            auto status = scanner_read_status(dev);
            return !status.is_motor_enabled && status.is_feeding_finished;
        }
        case AsicType::GL841: {
            auto reg = dev.interface->read_register(gl841::REG_0x40);

            return (!(reg & gl841::REG_0x40_DATAENB) && !(reg & gl841::REG_0x40_MOTMFLG));
        }
        case AsicType::GL843: {
            auto status = scanner_read_status(dev);
            auto reg = dev.interface->read_register(gl843::REG_0x40);

            return (!(reg & gl843::REG_0x40_DATAENB) && !(reg & gl843::REG_0x40_MOTMFLG) &&
                    !status.is_motor_enabled);
        }
        case AsicType::GL845:
        case AsicType::GL846: {
            auto status = scanner_read_status(dev);
            auto reg = dev.interface->read_register(gl846::REG_0x40);

            return (!(reg & gl846::REG_0x40_DATAENB) && !(reg & gl846::REG_0x40_MOTMFLG) &&
                    !status.is_motor_enabled);
        }
        case AsicType::GL847: {
            auto status = scanner_read_status(dev);
            auto reg = dev.interface->read_register(gl847::REG_0x40);

            return (!(reg & gl847::REG_0x40_DATAENB) && !(reg & gl847::REG_0x40_MOTMFLG) &&
                    !status.is_motor_enabled);
        }
        case AsicType::GL124: {
            auto status = scanner_read_status(dev);
            auto reg = dev.interface->read_register(gl124::REG_0x100);

            return (!(reg & gl124::REG_0x100_DATAENB) && !(reg & gl124::REG_0x100_MOTMFLG) &&
                    !status.is_motor_enabled);
        }
        default:
            throw SaneException("Unsupported asic type");
    }
}

void scanner_stop_action(Genesys_Device& dev)
{
    DBG_HELPER(dbg);

    switch (dev.model->asic_type) {
        case AsicType::GL841:
        case AsicType::GL843:
        case AsicType::GL845:
        case AsicType::GL846:
        case AsicType::GL847:
        case AsicType::GL124:
            break;
        default:
            throw SaneException("Unsupported asic type");
    }

    if (dev.cmd_set->needs_update_home_sensor_gpio()) {
        dev.cmd_set->update_home_sensor_gpio(dev);
    }

    if (scanner_is_motor_stopped(dev)) {
        DBG(DBG_info, "%s: already stopped\n", __func__);
        return;
    }

    scanner_stop_action_no_move(dev, dev.reg);

    if (is_testing_mode()) {
        return;
    }

    for (unsigned i = 0; i < 10; ++i) {
        if (scanner_is_motor_stopped(dev)) {
            return;
        }

        dev.interface->sleep_ms(100);
    }

    throw SaneException(SANE_STATUS_IO_ERROR, "could not stop motor");
}

void scanner_stop_action_no_move(Genesys_Device& dev, genesys::Genesys_Register_Set& regs)
{
    switch (dev.model->asic_type) {
        case AsicType::GL646:
        case AsicType::GL841:
        case AsicType::GL843:
        case AsicType::GL845:
        case AsicType::GL846:
        case AsicType::GL847:
        case AsicType::GL124:
            break;
        default:
            throw SaneException("Unsupported asic type");
    }

    regs_set_optical_off(dev.model->asic_type, regs);
    // same across all supported ASICs
    dev.interface->write_register(0x01, regs.get8(0x01));

    // looks like certain scanners lock up if we try to scan immediately after stopping previous
    // action.
    dev.interface->sleep_ms(100);
}

void scanner_move(Genesys_Device& dev, ScanMethod scan_method, unsigned steps, Direction direction)
{
    DBG_HELPER_ARGS(dbg, "steps=%d direction=%d", steps, static_cast<unsigned>(direction));

    auto local_reg = dev.reg;

    unsigned resolution = dev.model->get_resolution_settings(scan_method).get_min_resolution_y();

    const auto& sensor = sanei_genesys_find_sensor(&dev, resolution, 3, scan_method);

    bool uses_secondary_head = (scan_method == ScanMethod::TRANSPARENCY ||
                                scan_method == ScanMethod::TRANSPARENCY_INFRARED) &&
                               (!has_flag(dev.model->flags, ModelFlag::UTA_NO_SECONDARY_MOTOR));

    bool uses_secondary_pos = uses_secondary_head &&
                              dev.model->default_method == ScanMethod::FLATBED;

    if (!dev.is_head_pos_known(ScanHeadId::PRIMARY)) {
        throw SaneException("Unknown head position");
    }
    if (uses_secondary_pos && !dev.is_head_pos_known(ScanHeadId::SECONDARY)) {
        throw SaneException("Unknown head position");
    }
    if (direction == Direction::BACKWARD && steps > dev.head_pos(ScanHeadId::PRIMARY)) {
        throw SaneException("Trying to feed behind the home position %d %d",
                            steps, dev.head_pos(ScanHeadId::PRIMARY));
    }
    if (uses_secondary_pos && direction == Direction::BACKWARD &&
        steps > dev.head_pos(ScanHeadId::SECONDARY))
    {
        throw SaneException("Trying to feed behind the home position %d %d",
                            steps, dev.head_pos(ScanHeadId::SECONDARY));
    }

    ScanSession session;
    session.params.xres = resolution;
    session.params.yres = resolution;
    session.params.startx = 0;
    session.params.starty = steps;
    session.params.pixels = 50;
    session.params.lines = 3;
    session.params.depth = 8;
    session.params.channels = 1;
    session.params.scan_method = scan_method;
    session.params.scan_mode = ScanColorMode::GRAY;
    session.params.color_filter = ColorFilter::GREEN;

    session.params.flags = ScanFlag::DISABLE_SHADING |
                           ScanFlag::DISABLE_GAMMA |
                           ScanFlag::FEEDING |
                           ScanFlag::IGNORE_STAGGER_OFFSET |
                           ScanFlag::IGNORE_COLOR_OFFSET;

    if (dev.model->asic_type == AsicType::GL124) {
        session.params.flags |= ScanFlag::DISABLE_BUFFER_FULL_MOVE;
    }

    if (direction == Direction::BACKWARD) {
        session.params.flags |= ScanFlag::REVERSE;
    }

    compute_session(&dev, session, sensor);

    dev.cmd_set->init_regs_for_scan_session(&dev, sensor, &local_reg, session);

    if (dev.model->asic_type != AsicType::GL843) {
        regs_set_exposure(dev.model->asic_type, local_reg,
                          sanei_genesys_fixup_exposure({0, 0, 0}));
    }
    scanner_clear_scan_and_feed_counts(dev);

    dev.interface->write_registers(local_reg);
    if (uses_secondary_head) {
        dev.cmd_set->set_motor_mode(dev, local_reg, MotorMode::PRIMARY_AND_SECONDARY);
    }

    try {
        scanner_start_action(dev, true);
    } catch (...) {
        catch_all_exceptions(__func__, [&]() {
            dev.cmd_set->set_motor_mode(dev, local_reg, MotorMode::PRIMARY);
        });
        catch_all_exceptions(__func__, [&]() { scanner_stop_action(dev); });
        // restore original registers
        catch_all_exceptions(__func__, [&]() { dev.interface->write_registers(dev.reg); });
        throw;
    }

    if (is_testing_mode()) {
        dev.interface->test_checkpoint("feed");

        dev.advance_head_pos_by_steps(ScanHeadId::PRIMARY, direction, steps);
        if (uses_secondary_pos) {
            dev.advance_head_pos_by_steps(ScanHeadId::SECONDARY, direction, steps);
        }

        scanner_stop_action(dev);
        if (uses_secondary_head) {
            dev.cmd_set->set_motor_mode(dev, local_reg, MotorMode::PRIMARY);
        }
        return;
    }

    // wait until feed count reaches the required value
    // FIXME: should porbably wait for some timeout
    Status status;
    for (unsigned i = 0;; ++i) {
        status = scanner_read_status(dev);
        if (status.is_feeding_finished || (
            direction == Direction::BACKWARD && status.is_at_home))
        {
            break;
        }
        dev.interface->sleep_ms(10);
    }

    scanner_stop_action(dev);
    if (uses_secondary_head) {
        dev.cmd_set->set_motor_mode(dev, local_reg, MotorMode::PRIMARY);
    }

    dev.advance_head_pos_by_steps(ScanHeadId::PRIMARY, direction, steps);
    if (uses_secondary_pos) {
        dev.advance_head_pos_by_steps(ScanHeadId::SECONDARY, direction, steps);
    }

    // looks like certain scanners lock up if we scan immediately after feeding
    dev.interface->sleep_ms(100);
}

void scanner_move_back_home(Genesys_Device& dev, bool wait_until_home)
{
    DBG_HELPER_ARGS(dbg, "wait_until_home = %d", wait_until_home);

    switch (dev.model->asic_type) {
        case AsicType::GL843:
        case AsicType::GL845:
        case AsicType::GL846:
        case AsicType::GL847:
        case AsicType::GL124:
            break;
        default:
            throw SaneException("Unsupported asic type");
    }

    // FIXME: also check whether the scanner actually has a secondary head
    if ((!dev.is_head_pos_known(ScanHeadId::SECONDARY) ||
        dev.head_pos(ScanHeadId::SECONDARY) > 0 ||
        dev.settings.scan_method == ScanMethod::TRANSPARENCY ||
        dev.settings.scan_method == ScanMethod::TRANSPARENCY_INFRARED) &&
            (!has_flag(dev.model->flags, ModelFlag::UTA_NO_SECONDARY_MOTOR)))
    {
        scanner_move_back_home_ta(dev);
    }

    if (dev.is_head_pos_known(ScanHeadId::PRIMARY) &&
        dev.head_pos(ScanHeadId::PRIMARY) > 1000)
    {
        // leave 500 steps for regular slow back home
        scanner_move(dev, dev.model->default_method, dev.head_pos(ScanHeadId::PRIMARY) - 500,
                     Direction::BACKWARD);
    }

    if (dev.cmd_set->needs_update_home_sensor_gpio()) {
        dev.cmd_set->update_home_sensor_gpio(dev);
    }

    auto status = scanner_read_reliable_status(dev);

    if (status.is_at_home) {
        dbg.log(DBG_info, "already at home");
        dev.set_head_pos_zero(ScanHeadId::PRIMARY);
        return;
    }

    Genesys_Register_Set local_reg = dev.reg;
    unsigned resolution = sanei_genesys_get_lowest_ydpi(&dev);

    const auto& sensor = sanei_genesys_find_sensor(&dev, resolution, 1, dev.model->default_method);

    ScanSession session;
    session.params.xres = resolution;
    session.params.yres = resolution;
    session.params.startx = 0;
    session.params.starty = 40000;
    session.params.pixels = 50;
    session.params.lines = 3;
    session.params.depth = 8;
    session.params.channels = 1;
    session.params.scan_method = dev.settings.scan_method;
    session.params.scan_mode = ScanColorMode::GRAY;
    session.params.color_filter = ColorFilter::GREEN;

    session.params.flags =  ScanFlag::DISABLE_SHADING |
                            ScanFlag::DISABLE_GAMMA |
                            ScanFlag::IGNORE_STAGGER_OFFSET |
                            ScanFlag::IGNORE_COLOR_OFFSET |
                            ScanFlag::REVERSE;

    if (dev.model->asic_type == AsicType::GL843) {
        session.params.flags |= ScanFlag::DISABLE_BUFFER_FULL_MOVE;
    }

    compute_session(&dev, session, sensor);

    dev.cmd_set->init_regs_for_scan_session(&dev, sensor, &local_reg, session);

    scanner_clear_scan_and_feed_counts(dev);

    dev.interface->write_registers(local_reg);

    if (dev.model->asic_type == AsicType::GL124) {
        gl124::gl124_setup_scan_gpio(&dev, resolution);
    }

    try {
        scanner_start_action(dev, true);
    } catch (...) {
        catch_all_exceptions(__func__, [&]() { scanner_stop_action(dev); });
        // restore original registers
        catch_all_exceptions(__func__, [&]()
        {
            dev.interface->write_registers(dev.reg);
        });
        throw;
    }

    if (dev.cmd_set->needs_update_home_sensor_gpio()) {
        dev.cmd_set->update_home_sensor_gpio(dev);
    }

    if (is_testing_mode()) {
        dev.interface->test_checkpoint("move_back_home");
        dev.set_head_pos_zero(ScanHeadId::PRIMARY);
        return;
    }

    if (wait_until_home) {
        for (unsigned i = 0; i < 300; ++i) {
            auto status = scanner_read_status(dev);

            if (status.is_at_home) {
                dbg.log(DBG_info, "reached home position");
                if (dev.model->asic_type == AsicType::GL846 ||
                    dev.model->asic_type == AsicType::GL847)
                {
                    scanner_stop_action(dev);
                }
                dev.set_head_pos_zero(ScanHeadId::PRIMARY);
                return;
            }

            dev.interface->sleep_ms(100);
        }

        // when we come here then the scanner needed too much time for this, so we better stop
        // the motor
        catch_all_exceptions(__func__, [&](){ scanner_stop_action(dev); });
        dev.set_head_pos_unknown(ScanHeadId::PRIMARY | ScanHeadId::SECONDARY);
        throw SaneException(SANE_STATUS_IO_ERROR, "timeout while waiting for scanhead to go home");
    }
    dbg.log(DBG_info, "scanhead is still moving");
}

namespace {
    bool should_use_secondary_motor_mode(Genesys_Device& dev)
    {
        bool should_use = !dev.is_head_pos_known(ScanHeadId::SECONDARY) ||
                          !dev.is_head_pos_known(ScanHeadId::PRIMARY) ||
                          dev.head_pos(ScanHeadId::SECONDARY) > dev.head_pos(ScanHeadId::PRIMARY);
        bool supports = dev.model->model_id == ModelId::CANON_8600F;
        return should_use && supports;
    }

    void handle_motor_position_after_move_back_home_ta(Genesys_Device& dev, MotorMode motor_mode)
    {
        if (motor_mode == MotorMode::SECONDARY) {
            dev.set_head_pos_zero(ScanHeadId::SECONDARY);
            return;
        }

        if (dev.is_head_pos_known(ScanHeadId::PRIMARY)) {
            if (dev.head_pos(ScanHeadId::PRIMARY) > dev.head_pos(ScanHeadId::SECONDARY)) {
                dev.advance_head_pos_by_steps(ScanHeadId::PRIMARY, Direction::BACKWARD,
                                              dev.head_pos(ScanHeadId::SECONDARY));
            } else {
                dev.set_head_pos_zero(ScanHeadId::PRIMARY);
            }
            dev.set_head_pos_zero(ScanHeadId::SECONDARY);
        }
    }
} // namespace

void scanner_move_back_home_ta(Genesys_Device& dev)
{
    DBG_HELPER(dbg);

    switch (dev.model->asic_type) {
        case AsicType::GL843:
            break;
        default:
            throw SaneException("Unsupported asic type");
    }

    Genesys_Register_Set local_reg = dev.reg;

    auto scan_method = ScanMethod::TRANSPARENCY;
    unsigned resolution = dev.model->get_resolution_settings(scan_method).get_min_resolution_y();

    const auto& sensor = sanei_genesys_find_sensor(&dev, resolution, 1, scan_method);

    if (dev.is_head_pos_known(ScanHeadId::SECONDARY) &&
        dev.is_head_pos_known(ScanHeadId::PRIMARY) &&
        dev.head_pos(ScanHeadId::SECONDARY) > 1000 &&
        dev.head_pos(ScanHeadId::SECONDARY) <= dev.head_pos(ScanHeadId::PRIMARY))
    {
        // leave 500 steps for regular slow back home
        scanner_move(dev, scan_method, dev.head_pos(ScanHeadId::SECONDARY) - 500,
                     Direction::BACKWARD);
    }

    ScanSession session;
    session.params.xres = resolution;
    session.params.yres = resolution;
    session.params.startx = 0;
    session.params.starty = 40000;
    session.params.pixels = 50;
    session.params.lines = 3;
    session.params.depth = 8;
    session.params.channels = 1;
    session.params.scan_method = scan_method;
    session.params.scan_mode = ScanColorMode::GRAY;
    session.params.color_filter = ColorFilter::GREEN;

    session.params.flags =  ScanFlag::DISABLE_SHADING |
                            ScanFlag::DISABLE_GAMMA |
                            ScanFlag::IGNORE_STAGGER_OFFSET |
                            ScanFlag::IGNORE_COLOR_OFFSET |
                            ScanFlag::REVERSE;

    compute_session(&dev, session, sensor);

    dev.cmd_set->init_regs_for_scan_session(&dev, sensor, &local_reg, session);

    scanner_clear_scan_and_feed_counts(dev);

    dev.interface->write_registers(local_reg);

    auto motor_mode = should_use_secondary_motor_mode(dev) ? MotorMode::SECONDARY
                                                           : MotorMode::PRIMARY_AND_SECONDARY;

    dev.cmd_set->set_motor_mode(dev, local_reg, motor_mode);

    try {
        scanner_start_action(dev, true);
    } catch (...) {
        catch_all_exceptions(__func__, [&]() { scanner_stop_action(dev); });
        // restore original registers
        catch_all_exceptions(__func__, [&]() { dev.interface->write_registers(dev.reg); });
        throw;
    }

    if (is_testing_mode()) {
        dev.interface->test_checkpoint("move_back_home_ta");

        handle_motor_position_after_move_back_home_ta(dev, motor_mode);

        scanner_stop_action(dev);
        dev.cmd_set->set_motor_mode(dev, local_reg, MotorMode::PRIMARY);
        return;
    }

    for (unsigned i = 0; i < 1200; ++i) {

        auto status = scanner_read_status(dev);

        if (status.is_at_home) {
            dbg.log(DBG_info, "TA reached home position");

            handle_motor_position_after_move_back_home_ta(dev, motor_mode);

            scanner_stop_action(dev);
            dev.cmd_set->set_motor_mode(dev, local_reg, MotorMode::PRIMARY);
            return;
        }

        dev.interface->sleep_ms(100);
    }

    throw SaneException("Timeout waiting for XPA lamp to park");
}

namespace gl841 {
    void gl841_stop_action(Genesys_Device* dev);
} // namespace gl841

void scanner_search_strip(Genesys_Device& dev, bool forward, bool black)
{
    DBG_HELPER_ARGS(dbg, "%s %s", black ? "black" : "white", forward ? "forward" : "reverse");

    if (dev.model->asic_type == AsicType::GL841 && !black && forward) {
        dev.frontend.set_gain(0, 0xff);
        dev.frontend.set_gain(1, 0xff);
        dev.frontend.set_gain(2, 0xff);
    }

    // set up for a gray scan at lowest dpi
    const auto& resolution_settings = dev.model->get_resolution_settings(dev.settings.scan_method);
    unsigned dpi = resolution_settings.get_min_resolution_x();
    unsigned channels = 1;

    auto& sensor = sanei_genesys_find_sensor(&dev, dpi, channels, dev.settings.scan_method);
    dev.cmd_set->set_fe(&dev, sensor, AFE_SET);
    scanner_stop_action(dev);


    // shading calibration is done with dev.motor.base_ydpi
    unsigned lines = static_cast<unsigned>(dev.model->y_size_calib_mm * dpi / MM_PER_INCH);
    if (dev.model->asic_type == AsicType::GL841) {
        lines = 10; // TODO: use dev.model->search_lines
        lines = static_cast<unsigned>((lines * dpi) / MM_PER_INCH);
    }

    unsigned pixels = dev.model->x_size_calib_mm * dpi / MM_PER_INCH;

    dev.set_head_pos_zero(ScanHeadId::PRIMARY);

    unsigned length = 20;
    if (dev.model->asic_type == AsicType::GL841) {
        // 20 cm max length for calibration sheet
        length = static_cast<unsigned>(((200 * dpi) / MM_PER_INCH) / lines);
    }

    auto local_reg = dev.reg;

    ScanSession session;
    session.params.xres = dpi;
    session.params.yres = dpi;
    session.params.startx = 0;
    session.params.starty = 0;
    session.params.pixels = pixels;
    session.params.lines = lines;
    session.params.depth = 8;
    session.params.channels = channels;
    session.params.scan_method = dev.settings.scan_method;
    session.params.scan_mode = ScanColorMode::GRAY;
    session.params.color_filter = ColorFilter::RED;
    session.params.flags = ScanFlag::DISABLE_SHADING |
                           ScanFlag::DISABLE_GAMMA;
    if (dev.model->asic_type != AsicType::GL841 && !forward) {
        session.params.flags |= ScanFlag::REVERSE;
    }
    compute_session(&dev, session, sensor);

    dev.cmd_set->init_regs_for_scan_session(&dev, sensor, &local_reg, session);

    dev.interface->write_registers(local_reg);

    dev.cmd_set->begin_scan(&dev, sensor, &local_reg, true);

    if (is_testing_mode()) {
        dev.interface->test_checkpoint("search_strip");
        if (dev.model->asic_type == AsicType::GL841) {
            gl841::gl841_stop_action(&dev);
        } else {
            scanner_stop_action(dev);
        }
        return;
    }

    wait_until_buffer_non_empty(&dev);

    // now we're on target, we can read data
    auto image = read_unshuffled_image_from_scanner(&dev, session, session.output_total_bytes);

    if (dev.model->asic_type == AsicType::GL841) {
        gl841::gl841_stop_action(&dev);
    } else {
        scanner_stop_action(dev);
    }

    unsigned pass = 0;
    if (DBG_LEVEL >= DBG_data) {
        char title[80];
        std::sprintf(title, "gl_search_strip_%s_%s%02d.pnm",
                     black ? "black" : "white", forward ? "fwd" : "bwd", pass);
        sanei_genesys_write_pnm_file(title, image);
    }

    // loop until strip is found or maximum pass number done
    bool found = false;
    while (pass < length && !found) {
        dev.interface->write_registers(local_reg);

        // now start scan
        dev.cmd_set->begin_scan(&dev, sensor, &local_reg, true);

        wait_until_buffer_non_empty(&dev);

        // now we're on target, we can read data
        image = read_unshuffled_image_from_scanner(&dev, session, session.output_total_bytes);

        scanner_stop_action(dev);

        if (DBG_LEVEL >= DBG_data) {
            char title[80];
            std::sprintf(title, "gl_search_strip_%s_%s%02d.pnm",
                         black ? "black" : "white",
                         forward ? "fwd" : "bwd", static_cast<int>(pass));
            sanei_genesys_write_pnm_file(title, image);
        }

        unsigned white_level = 90;
        unsigned black_level = 60;

        std::size_t count = 0;
        // Search data to find black strip
        // When searching forward, we only need one line of the searched color since we
        // will scan forward. But when doing backward search, we need all the area of the ame color
        if (forward) {

            for (std::size_t y = 0; y < image.get_height() && !found; y++) {
                count = 0;

                // count of white/black pixels depending on the color searched
                for (std::size_t x = 0; x < image.get_width(); x++) {

                    // when searching for black, detect white pixels
                    if (black && image.get_raw_channel(x, y, 0) > white_level) {
                        count++;
                    }

                    // when searching for white, detect black pixels
                    if (!black && image.get_raw_channel(x, y, 0) < black_level) {
                        count++;
                    }
                }

                // at end of line, if count >= 3%, line is not fully of the desired color
                // so we must go to next line of the buffer */
                // count*100/pixels < 3

                auto found_percentage = (count * 100 / image.get_width());
                if (found_percentage < 3) {
                    found = 1;
                    DBG(DBG_data, "%s: strip found forward during pass %d at line %zu\n", __func__,
                        pass, y);
                } else {
                    DBG(DBG_data, "%s: pixels=%zu, count=%zu (%zu%%)\n", __func__,
                        image.get_width(), count, found_percentage);
                }
            }
        } else {
            /*  since calibration scans are done forward, we need the whole area
                to be of the required color when searching backward
            */
            count = 0;
            for (std::size_t y = 0; y < image.get_height(); y++) {
                // count of white/black pixels depending on the color searched
                for (std::size_t x = 0; x < image.get_width(); x++) {
                    // when searching for black, detect white pixels
                    if (black && image.get_raw_channel(x, y, 0) > white_level) {
                        count++;
                    }
                    // when searching for white, detect black pixels
                    if (!black && image.get_raw_channel(x, y, 0) < black_level) {
                        count++;
                    }
                }
            }

            // at end of area, if count >= 3%, area is not fully of the desired color
            // so we must go to next buffer
            auto found_percentage = count * 100 / (image.get_width() * image.get_height());
            if (found_percentage < 3) {
                found = 1;
                DBG(DBG_data, "%s: strip found backward during pass %d \n", __func__, pass);
            } else {
                DBG(DBG_data, "%s: pixels=%zu, count=%zu (%zu%%)\n", __func__, image.get_width(),
                    count, found_percentage);
            }
        }
        pass++;
    }

    if (found) {
        DBG(DBG_info, "%s: %s strip found\n", __func__, black ? "black" : "white");
    } else {
        throw SaneException(SANE_STATUS_UNSUPPORTED, "%s strip not found",
                            black ? "black" : "white");
    }
}


void sanei_genesys_calculate_zmod(bool two_table,
                                  uint32_t exposure_time,
                                  const std::vector<uint16_t>& slope_table,
                                  unsigned acceleration_steps,
                                  unsigned move_steps,
                                  unsigned buffer_acceleration_steps,
                                  uint32_t* out_z1, uint32_t* out_z2)
{
    // acceleration total time
    unsigned sum = std::accumulate(slope_table.begin(), slope_table.begin() + acceleration_steps,
                                   0, std::plus<unsigned>());

    /* Z1MOD:
        c = sum(slope_table; reg_stepno)
        d = reg_fwdstep * <cruising speed>
        Z1MOD = (c+d) % exposure_time
    */
    *out_z1 = (sum + buffer_acceleration_steps * slope_table[acceleration_steps - 1]) % exposure_time;

    /* Z2MOD:
        a = sum(slope_table; reg_stepno)
        b = move_steps or 1 if 2 tables
        Z1MOD = (a+b) % exposure_time
    */
    if (!two_table) {
        sum = sum + (move_steps * slope_table[acceleration_steps - 1]);
    } else {
        sum = sum + slope_table[acceleration_steps - 1];
    }
    *out_z2 = sum % exposure_time;
}

/**
 * scans a white area with motor and lamp off to get the per CCD pixel offset
 * that will be used to compute shading coefficient
 * @param dev scanner's device
 */
static void genesys_shading_calibration_impl(Genesys_Device* dev, const Genesys_Sensor& sensor,
                                             Genesys_Register_Set& local_reg,
                                             std::vector<std::uint16_t>& out_average_data,
                                             bool is_dark, const std::string& log_filename_prefix)
{
    DBG_HELPER(dbg);

    if (dev->model->asic_type == AsicType::GL646) {
        dev->cmd_set->init_regs_for_shading(dev, sensor, local_reg);
        local_reg = dev->reg;
    } else {
        local_reg = dev->reg;
        dev->cmd_set->init_regs_for_shading(dev, sensor, local_reg);
        dev->interface->write_registers(local_reg);
    }

    debug_dump(DBG_info, dev->calib_session);

  size_t size;
  uint32_t pixels_per_line;

    if (dev->model->asic_type == AsicType::GL843) {
        pixels_per_line = dev->calib_session.output_pixels;
    } else {
        pixels_per_line = dev->calib_session.params.pixels;
    }
    unsigned channels = dev->calib_session.params.channels;

    // BUG: we are using wrong pixel number here
    unsigned start_offset =
            dev->calib_session.params.startx * sensor.optical_res / dev->calib_session.params.xres;
    unsigned out_pixels_per_line = pixels_per_line + start_offset;

    // FIXME: we set this during both dark and white calibration. A cleaner approach should
    // probably be used
    dev->average_size = channels * out_pixels_per_line;

    out_average_data.clear();
    out_average_data.resize(dev->average_size);

    if (is_dark && dev->settings.scan_method == ScanMethod::TRANSPARENCY_INFRARED) {
        // FIXME: dark shading currently not supported on infrared transparency scans
        return;
    }

    // FIXME: the current calculation is likely incorrect on non-GL843 implementations,
    // but this needs checking. Note the extra line when computing size.
    if (dev->model->asic_type == AsicType::GL843) {
        size = dev->calib_session.output_total_bytes_raw;
    } else {
        size = channels * 2 * pixels_per_line * (dev->calib_session.params.lines + 1);
    }

  std::vector<uint16_t> calibration_data(size / 2);

    // turn off motor and lamp power for flatbed scanners, but not for sheetfed scanners
    // because they have a calibration sheet with a sufficient black strip
    if (is_dark && !dev->model->is_sheetfed) {
        sanei_genesys_set_lamp_power(dev, sensor, local_reg, false);
    } else {
        sanei_genesys_set_lamp_power(dev, sensor, local_reg, true);
    }
    sanei_genesys_set_motor_power(local_reg, true);

    dev->interface->write_registers(local_reg);

    if (is_dark) {
        // wait some time to let lamp to get dark
        dev->interface->sleep_ms(200);
    } else if (has_flag(dev->model->flags, ModelFlag::DARK_CALIBRATION)) {
        // make sure lamp is bright again
        // FIXME: what about scanners that take a long time to warm the lamp?
        dev->interface->sleep_ms(500);
    }

    bool start_motor = !is_dark;
    dev->cmd_set->begin_scan(dev, sensor, &local_reg, start_motor);


    if (is_testing_mode()) {
        dev->interface->test_checkpoint(is_dark ? "dark_shading_calibration"
                                                : "white_shading_calibration");
        dev->cmd_set->end_scan(dev, &local_reg, true);
        return;
    }

    sanei_genesys_read_data_from_scanner(dev, reinterpret_cast<std::uint8_t*>(calibration_data.data()),
                                         size);

    dev->cmd_set->end_scan(dev, &local_reg, true);

    if (has_flag(dev->model->flags, ModelFlag::INVERTED_16BIT_DATA)) {
        for (std::size_t i = 0; i < size / 2; ++i) {
            auto value = calibration_data[i];
            value = ((value >> 8) & 0xff) | ((value << 8) & 0xff00);
            calibration_data[i] = value;
        }
    }

    std::fill(out_average_data.begin(),
              out_average_data.begin() + start_offset * channels, 0);

    compute_array_percentile_approx(out_average_data.data() +
                                        start_offset * channels,
                                    calibration_data.data(),
                                    dev->calib_session.params.lines, pixels_per_line * channels,
                                    0.5f);

    if (DBG_LEVEL >= DBG_data) {
        sanei_genesys_write_pnm_file16((log_filename_prefix + "_shading.pnm").c_str(),
                                       calibration_data.data(),
                                       channels, pixels_per_line, dev->calib_session.params.lines);
        sanei_genesys_write_pnm_file16((log_filename_prefix + "_average.pnm").c_str(),
                                       out_average_data.data(),
                                       channels, out_pixels_per_line, 1);
    }
}


static void genesys_dark_shading_calibration(Genesys_Device* dev, const Genesys_Sensor& sensor,
                                             Genesys_Register_Set& local_reg)
{
    DBG_HELPER(dbg);
    genesys_shading_calibration_impl(dev, sensor, local_reg, dev->dark_average_data, true,
                                     "gl_black");
}
/*
 * this function builds dummy dark calibration data so that we can
 * compute shading coefficient in a clean way
 *  todo: current values are hardcoded, we have to find if they
 * can be computed from previous calibration data (when doing offset
 * calibration ?)
 */
static void genesys_dummy_dark_shading(Genesys_Device* dev, const Genesys_Sensor& sensor)
{
    DBG_HELPER(dbg);
  uint32_t pixels_per_line;
  uint32_t skip, xend;
  int dummy1, dummy2, dummy3;	/* dummy black average per channel */

    if (dev->model->asic_type == AsicType::GL843) {
        pixels_per_line = dev->calib_session.output_pixels;
    } else {
        pixels_per_line = dev->calib_session.params.pixels;
    }

    unsigned channels = dev->calib_session.params.channels;

    // BUG: we are using wrong pixel number here
    unsigned start_offset =
            dev->calib_session.params.startx * sensor.optical_res / dev->calib_session.params.xres;

    unsigned out_pixels_per_line = pixels_per_line + start_offset;

    dev->average_size = channels * out_pixels_per_line;
  dev->dark_average_data.clear();
  dev->dark_average_data.resize(dev->average_size, 0);

  /* we average values on 'the left' where CCD pixels are under casing and
     give darkest values. We then use these as dummy dark calibration */
  if (dev->settings.xres <= sensor.optical_res / 2)
    {
      skip = 4;
      xend = 36;
    }
  else
    {
      skip = 4;
      xend = 68;
    }
    if (dev->model->sensor_id==SensorId::CCD_G4050 ||
        dev->model->sensor_id==SensorId::CCD_HP_4850C
     || dev->model->sensor_id==SensorId::CCD_CANON_4400F
     || dev->model->sensor_id==SensorId::CCD_CANON_8400F
     || dev->model->sensor_id==SensorId::CCD_KVSS080)
    {
      skip = 2;
      xend = sensor.black_pixels;
    }

  /* average each channels on half left margin */
  dummy1 = 0;
  dummy2 = 0;
  dummy3 = 0;

    for (unsigned x = skip + 1; x <= xend; x++) {
        dummy1 += dev->white_average_data[channels * x];
        if (channels > 1) {
            dummy2 += dev->white_average_data[channels * x + 1];
            dummy3 += dev->white_average_data[channels * x + 2];
        }
    }

  dummy1 /= (xend - skip);
  if (channels > 1)
    {
      dummy2 /= (xend - skip);
      dummy3 /= (xend - skip);
    }
  DBG(DBG_proc, "%s: dummy1=%d, dummy2=%d, dummy3=%d \n", __func__, dummy1, dummy2, dummy3);

  /* fill dark_average */
    for (unsigned x = 0; x < out_pixels_per_line; x++) {
        dev->dark_average_data[channels * x] = dummy1;
        if (channels > 1) {
            dev->dark_average_data[channels * x + 1] = dummy2;
            dev->dark_average_data[channels * x + 2] = dummy3;
        }
    }
}


static void genesys_repark_sensor_before_shading(Genesys_Device* dev)
{
    DBG_HELPER(dbg);
    if (has_flag(dev->model->flags, ModelFlag::SHADING_REPARK)) {
        dev->cmd_set->move_back_home(dev, true);

        if (dev->settings.scan_method == ScanMethod::TRANSPARENCY ||
            dev->settings.scan_method == ScanMethod::TRANSPARENCY_INFRARED)
        {
            dev->cmd_set->move_to_ta(dev);
        }
    }
}

static void genesys_repark_sensor_after_white_shading(Genesys_Device* dev)
{
    DBG_HELPER(dbg);
    if (has_flag(dev->model->flags, ModelFlag::SHADING_REPARK)) {
        dev->cmd_set->move_back_home(dev, true);
    }
}

static void genesys_white_shading_calibration(Genesys_Device* dev, const Genesys_Sensor& sensor,
                                              Genesys_Register_Set& local_reg)
{
    DBG_HELPER(dbg);
    genesys_shading_calibration_impl(dev, sensor, local_reg, dev->white_average_data, false,
                                     "gl_white");
}

// This calibration uses a scan over the calibration target, comprising a black and a white strip.
// (So the motor must be on.)
static void genesys_dark_white_shading_calibration(Genesys_Device* dev,
                                                   const Genesys_Sensor& sensor,
                                                   Genesys_Register_Set& local_reg)
{
    DBG_HELPER(dbg);

    if (dev->model->asic_type == AsicType::GL646) {
        dev->cmd_set->init_regs_for_shading(dev, sensor, local_reg);
        local_reg = dev->reg;
    } else {
        local_reg = dev->reg;
        dev->cmd_set->init_regs_for_shading(dev, sensor, local_reg);
        dev->interface->write_registers(local_reg);
    }

  size_t size;
  uint32_t pixels_per_line;
  unsigned int x;
  uint32_t dark, white, dark_sum, white_sum, dark_count, white_count, col,
    dif;

    if (dev->model->asic_type == AsicType::GL843) {
        pixels_per_line = dev->calib_session.output_pixels;
    } else {
        pixels_per_line = dev->calib_session.params.pixels;
    }

    unsigned channels = dev->calib_session.params.channels;

    // BUG: we are using wrong pixel number here
    unsigned start_offset =
            dev->calib_session.params.startx * sensor.optical_res / dev->calib_session.params.xres;

    unsigned out_pixels_per_line = pixels_per_line + start_offset;

    dev->average_size = channels * out_pixels_per_line;

  dev->white_average_data.clear();
  dev->white_average_data.resize(dev->average_size);

  dev->dark_average_data.clear();
  dev->dark_average_data.resize(dev->average_size);

    if (dev->model->asic_type == AsicType::GL843) {
        size = dev->calib_session.output_total_bytes_raw;
    } else {
        // FIXME: on GL841 this is different than dev->calib_session.output_total_bytes_raw,
        // needs checking
        size = channels * 2 * pixels_per_line * dev->calib_session.params.lines;
    }

  std::vector<uint8_t> calibration_data(size);

    // turn on motor and lamp power
    sanei_genesys_set_lamp_power(dev, sensor, local_reg, true);
    sanei_genesys_set_motor_power(local_reg, true);

    dev->interface->write_registers(local_reg);

    dev->cmd_set->begin_scan(dev, sensor, &local_reg, false);

    if (is_testing_mode()) {
        dev->interface->test_checkpoint("dark_white_shading_calibration");
        dev->cmd_set->end_scan(dev, &local_reg, true);
        return;
    }

    sanei_genesys_read_data_from_scanner(dev, calibration_data.data(), size);

    dev->cmd_set->end_scan(dev, &local_reg, true);

  if (DBG_LEVEL >= DBG_data)
    {
      if (dev->model->is_cis)
        {
          sanei_genesys_write_pnm_file("gl_black_white_shading.pnm", calibration_data.data(),
                                       16, 1, pixels_per_line*channels,
                                       dev->calib_session.params.lines);
        }
      else
        {
          sanei_genesys_write_pnm_file("gl_black_white_shading.pnm", calibration_data.data(),
                                       16, channels, pixels_per_line,
                                       dev->calib_session.params.lines);
        }
    }


    std::fill(dev->dark_average_data.begin(),
              dev->dark_average_data.begin() + start_offset * channels, 0);
    std::fill(dev->white_average_data.begin(),
              dev->white_average_data.begin() + start_offset * channels, 0);

    uint16_t* average_white = dev->white_average_data.data() +
                              start_offset * channels;
    uint16_t* average_dark = dev->dark_average_data.data() +
                             start_offset * channels;

  for (x = 0; x < pixels_per_line * channels; x++)
    {
      dark = 0xffff;
      white = 0;

            for (std::size_t y = 0; y < dev->calib_session.params.lines; y++)
	{
	  col = calibration_data[(x + y * pixels_per_line * channels) * 2];
	  col |=
	    calibration_data[(x + y * pixels_per_line * channels) * 2 +
			     1] << 8;

	  if (col > white)
	    white = col;
	  if (col < dark)
	    dark = col;
	}

      dif = white - dark;

      dark = dark + dif / 8;
      white = white - dif / 8;

      dark_count = 0;
      dark_sum = 0;

      white_count = 0;
      white_sum = 0;

            for (std::size_t y = 0; y < dev->calib_session.params.lines; y++)
	{
	  col = calibration_data[(x + y * pixels_per_line * channels) * 2];
	  col |=
	    calibration_data[(x + y * pixels_per_line * channels) * 2 +
			     1] << 8;

	  if (col >= white)
	    {
	      white_sum += col;
	      white_count++;
	    }
	  if (col <= dark)
	    {
	      dark_sum += col;
	      dark_count++;
	    }

	}

      dark_sum /= dark_count;
      white_sum /= white_count;

        *average_dark++ = dark_sum;
        *average_white++ = white_sum;
    }

    if (DBG_LEVEL >= DBG_data) {
        sanei_genesys_write_pnm_file16("gl_white_average.pnm", dev->white_average_data.data(),
                                       channels, out_pixels_per_line, 1);
        sanei_genesys_write_pnm_file16("gl_dark_average.pnm", dev->dark_average_data.data(),
                                       channels, out_pixels_per_line, 1);
    }
}

/* computes one coefficient given bright-dark value
 * @param coeff factor giving 1.00 gain
 * @param target desired target code
 * @param value brght-dark value
 * */
static unsigned int
compute_coefficient (unsigned int coeff, unsigned int target, unsigned int value)
{
  int result;

  if (value > 0)
    {
      result = (coeff * target) / value;
      if (result >= 65535)
	{
	  result = 65535;
	}
    }
  else
    {
      result = coeff;
    }
  return result;
}

/** @brief compute shading coefficients for LiDE scanners
 * The dark/white shading is actually performed _after_ reducing
 * resolution via averaging. only dark/white shading data for what would be
 * first pixel at full resolution is used.
 *
 * scanner raw input to output value calculation:
 *   o=(i-off)*(gain/coeff)
 *
 * from datasheet:
 *   off=dark_average
 *   gain=coeff*bright_target/(bright_average-dark_average)
 * works for dark_target==0
 *
 * what we want is these:
 *   bright_target=(bright_average-off)*(gain/coeff)
 *   dark_target=(dark_average-off)*(gain/coeff)
 * leading to
 *  off = (dark_average*bright_target - bright_average*dark_target)/(bright_target - dark_target)
 *  gain = (bright_target - dark_target)/(bright_average - dark_average)*coeff
 *
 * @param dev scanner's device
 * @param shading_data memory area where to store the computed shading coefficients
 * @param pixels_per_line number of pixels per line
 * @param words_per_color memory words per color channel
 * @param channels number of color channels (actually 1 or 3)
 * @param o shading coefficients left offset
 * @param coeff 4000h or 2000h depending on fast scan mode or not (GAIN4 bit)
 * @param target_bright value of the white target code
 * @param target_dark value of the black target code
*/
static void
compute_averaged_planar (Genesys_Device * dev, const Genesys_Sensor& sensor,
			 uint8_t * shading_data,
			 unsigned int pixels_per_line,
			 unsigned int words_per_color,
			 unsigned int channels,
			 unsigned int o,
			 unsigned int coeff,
			 unsigned int target_bright,
			 unsigned int target_dark)
{
  unsigned int x, i, j, br, dk, res, avgpixels, basepixels, val;
  unsigned int fill,factor;

  DBG(DBG_info, "%s: pixels=%d, offset=%d\n", __func__, pixels_per_line, o);

  /* initialize result */
  memset (shading_data, 0xff, words_per_color * 3 * 2);

  /*
     strangely i can write 0x20000 bytes beginning at 0x00000 without overwriting
     slope tables - which begin at address 0x10000(for 1200dpi hw mode):
     memory is organized in words(2 bytes) instead of single bytes. explains
     quite some things
   */
/*
  another one: the dark/white shading is actually performed _after_ reducing
  resolution via averaging. only dark/white shading data for what would be
  first pixel at full resolution is used.
 */
/*
  scanner raw input to output value calculation:
    o=(i-off)*(gain/coeff)

  from datasheet:
    off=dark_average
    gain=coeff*bright_target/(bright_average-dark_average)
  works for dark_target==0

  what we want is these:
    bright_target=(bright_average-off)*(gain/coeff)
    dark_target=(dark_average-off)*(gain/coeff)
  leading to
    off = (dark_average*bright_target - bright_average*dark_target)/(bright_target - dark_target)
    gain = (bright_target - dark_target)/(bright_average - dark_average)*coeff
 */
  res = dev->settings.xres;

    if (sensor.get_ccd_size_divisor_for_dpi(dev->settings.xres) > 1)
    {
        res *= 2;
    }

  /* this should be evenly dividable */
  basepixels = sensor.optical_res / res;

  /* gl841 supports 1/1 1/2 1/3 1/4 1/5 1/6 1/8 1/10 1/12 1/15 averaging */
  if (basepixels < 1)
    avgpixels = 1;
  else if (basepixels < 6)
    avgpixels = basepixels;
  else if (basepixels < 8)
    avgpixels = 6;
  else if (basepixels < 10)
    avgpixels = 8;
  else if (basepixels < 12)
    avgpixels = 10;
  else if (basepixels < 15)
    avgpixels = 12;
  else
    avgpixels = 15;

  /* LiDE80 packs shading data */
    if (dev->model->sensor_id != SensorId::CIS_CANON_LIDE_80) {
      factor=1;
      fill=avgpixels;
    }
  else
    {
      factor=avgpixels;
      fill=1;
    }

  DBG(DBG_info, "%s: averaging over %d pixels\n", __func__, avgpixels);
  DBG(DBG_info, "%s: packing factor is %d\n", __func__, factor);
  DBG(DBG_info, "%s: fill length is %d\n", __func__, fill);

  for (x = 0; x <= pixels_per_line - avgpixels; x += avgpixels)
    {
      if ((x + o) * 2 * 2 + 3 > words_per_color * 2)
	break;

      for (j = 0; j < channels; j++)
	{

	  dk = 0;
	  br = 0;
	  for (i = 0; i < avgpixels; i++)
	    {
                // dark data
                dk += dev->dark_average_data[(x + i + pixels_per_line * j)];
                // white data
                br += dev->white_average_data[(x + i + pixels_per_line * j)];
	    }

	  br /= avgpixels;
	  dk /= avgpixels;

	  if (br * target_dark > dk * target_bright)
	    val = 0;
	  else if (dk * target_bright - br * target_dark >
		   65535 * (target_bright - target_dark))
	    val = 65535;
	  else
            {
	      val = (dk * target_bright - br * target_dark) / (target_bright - target_dark);
            }

          /*fill all pixels, even if only the last one is relevant*/
	  for (i = 0; i < fill; i++)
	    {
	      shading_data[(x/factor + o + i) * 2 * 2 + words_per_color * 2 * j] = val & 0xff;
	      shading_data[(x/factor + o + i) * 2 * 2 + words_per_color * 2 * j + 1] = val >> 8;
	    }

	  val = br - dk;

	  if (65535 * val > (target_bright - target_dark) * coeff)
            {
	      val = (coeff * (target_bright - target_dark)) / val;
            }
	  else
            {
	      val = 65535;
            }

          /*fill all pixels, even if only the last one is relevant*/
	  for (i = 0; i < fill; i++)
	    {
	      shading_data[(x/factor + o + i) * 2 * 2 + words_per_color * 2 * j + 2] = val & 0xff;
	      shading_data[(x/factor + o + i) * 2 * 2 + words_per_color * 2 * j + 3] = val >> 8;
	    }
	}

      /* fill remaining channels */
      for (j = channels; j < 3; j++)
	{
	  for (i = 0; i < fill; i++)
	    {
	      shading_data[(x/factor + o + i) * 2 * 2 + words_per_color * 2 * j    ] = shading_data[(x/factor + o + i) * 2 * 2    ];
	      shading_data[(x/factor + o + i) * 2 * 2 + words_per_color * 2 * j + 1] = shading_data[(x/factor + o + i) * 2 * 2 + 1];
	      shading_data[(x/factor + o + i) * 2 * 2 + words_per_color * 2 * j + 2] = shading_data[(x/factor + o + i) * 2 * 2 + 2];
	      shading_data[(x/factor + o + i) * 2 * 2 + words_per_color * 2 * j + 3] = shading_data[(x/factor + o + i) * 2 * 2 + 3];
	    }
	}
    }
}

static std::array<unsigned, 3> color_order_to_cmat(ColorOrder color_order)
{
    switch (color_order) {
        case ColorOrder::RGB: return {0, 1, 2};
        case ColorOrder::GBR: return {2, 0, 1};
        default:
            throw std::logic_error("Unknown color order");
    }
}

/**
 * Computes shading coefficient using formula in data sheet. 16bit data values
 * manipulated here are little endian. For now we assume deletion scanning type
 * and that there is always 3 channels.
 * @param dev scanner's device
 * @param shading_data memory area where to store the computed shading coefficients
 * @param pixels_per_line number of pixels per line
 * @param channels number of color channels (actually 1 or 3)
 * @param cmat color transposition matrix
 * @param offset shading coefficients left offset
 * @param coeff 4000h or 2000h depending on fast scan mode or not
 * @param target value of the target code
 */
static void compute_coefficients(Genesys_Device * dev,
		      uint8_t * shading_data,
		      unsigned int pixels_per_line,
		      unsigned int channels,
                                 ColorOrder color_order,
		      int offset,
		      unsigned int coeff,
		      unsigned int target)
{
  uint8_t *ptr;			/* contain 16bit words in little endian */
  unsigned int x, c;
  unsigned int val, br, dk;
  unsigned int start, end;

  DBG(DBG_io, "%s: pixels_per_line=%d,  coeff=0x%04x\n", __func__, pixels_per_line, coeff);

    auto cmat = color_order_to_cmat(color_order);

  /* compute start & end values depending of the offset */
  if (offset < 0)
   {
      start = -1 * offset;
      end = pixels_per_line;
   }
  else
   {
     start = 0;
     end = pixels_per_line - offset;
   }

  for (c = 0; c < channels; c++)
    {
      for (x = start; x < end; x++)
	{
	  /* TODO if channels=1 , use filter to know the base addr */
	  ptr = shading_data + 4 * ((x + offset) * channels + cmat[c]);

        // dark data
        dk = dev->dark_average_data[x * channels + c];

        // white data
        br = dev->white_average_data[x * channels + c];

	  /* compute coeff */
	  val=compute_coefficient(coeff,target,br-dk);

	  /* assign it */
	  ptr[0] = dk & 255;
	  ptr[1] = dk / 256;
	  ptr[2] = val & 0xff;
	  ptr[3] = val / 256;

	}
    }
}

/**
 * Computes shading coefficient using formula in data sheet. 16bit data values
 * manipulated here are little endian. Data is in planar form, ie grouped by
 * lines of the same color component.
 * @param dev scanner's device
 * @param shading_data memory area where to store the computed shading coefficients
 * @param factor averaging factor when the calibration scan is done at a higher resolution
 * than the final scan
 * @param pixels_per_line number of pixels per line
 * @param words_per_color total number of shading data words for one color element
 * @param channels number of color channels (actually 1 or 3)
 * @param cmat transcoding matrix for color channel order
 * @param offset shading coefficients left offset
 * @param coeff 4000h or 2000h depending on fast scan mode or not
 * @param target white target value
 */
static void compute_planar_coefficients(Genesys_Device * dev,
			     uint8_t * shading_data,
			     unsigned int factor,
			     unsigned int pixels_per_line,
			     unsigned int words_per_color,
			     unsigned int channels,
                                        ColorOrder color_order,
			     unsigned int offset,
			     unsigned int coeff,
			     unsigned int target)
{
  uint8_t *ptr;			/* contains 16bit words in little endian */
  uint32_t x, c, i;
  uint32_t val, dk, br;

    auto cmat = color_order_to_cmat(color_order);

  DBG(DBG_io, "%s: factor=%d, pixels_per_line=%d, words=0x%X, coeff=0x%04x\n", __func__, factor,
      pixels_per_line, words_per_color, coeff);
  for (c = 0; c < channels; c++)
    {
      /* shading data is larger than pixels_per_line so offset can be neglected */
      for (x = 0; x < pixels_per_line; x+=factor)
	{
	  /* x2 because of 16 bit values, and x2 since one coeff for dark
	   * and another for white */
	  ptr = shading_data + words_per_color * cmat[c] * 2 + (x + offset) * 4;

	  dk = 0;
	  br = 0;

	  /* average case */
	  for(i=0;i<factor;i++)
	  {
                dk += dev->dark_average_data[((x+i) + pixels_per_line * c)];
                br += dev->white_average_data[((x+i) + pixels_per_line * c)];
	  }
	  dk /= factor;
	  br /= factor;

	  val = compute_coefficient (coeff, target, br - dk);

	  /* we duplicate the information to have calibration data at optical resolution */
	  for (i = 0; i < factor; i++)
	    {
	      ptr[0 + 4 * i] = dk & 255;
	      ptr[1 + 4 * i] = dk / 256;
	      ptr[2 + 4 * i] = val & 0xff;
	      ptr[3 + 4 * i] = val / 256;
	    }
	}
    }
  /* in case of gray level scan, we duplicate shading information on all
   * three color channels */
  if(channels==1)
  {
	  memcpy(shading_data+cmat[1]*2*words_per_color,
	         shading_data+cmat[0]*2*words_per_color,
		 words_per_color*2);
	  memcpy(shading_data+cmat[2]*2*words_per_color,
	         shading_data+cmat[0]*2*words_per_color,
		 words_per_color*2);
  }
}

static void
compute_shifted_coefficients (Genesys_Device * dev,
                              const Genesys_Sensor& sensor,
			      uint8_t * shading_data,
			      unsigned int pixels_per_line,
			      unsigned int channels,
                              ColorOrder color_order,
			      int offset,
			      unsigned int coeff,
			      unsigned int target_dark,
			      unsigned int target_bright,
			      unsigned int patch_size)		/* contigous extent */
{
  unsigned int x, avgpixels, basepixels, i, j, val1, val2;
  unsigned int br_tmp [3], dk_tmp [3];
  uint8_t *ptr = shading_data + offset * 3 * 4;                 /* contain 16bit words in little endian */
  unsigned int patch_cnt = offset * 3;                          /* at start, offset of first patch */

    auto cmat = color_order_to_cmat(color_order);

  x = dev->settings.xres;
  if (sensor.get_ccd_size_divisor_for_dpi(dev->settings.xres) > 1)
    x *= 2;							/* scanner is using half-ccd mode */
  basepixels = sensor.optical_res / x;			/*this should be evenly dividable */

      /* gl841 supports 1/1 1/2 1/3 1/4 1/5 1/6 1/8 1/10 1/12 1/15 averaging */
      if (basepixels < 1)
        avgpixels = 1;
      else if (basepixels < 6)
        avgpixels = basepixels;
      else if (basepixels < 8)
        avgpixels = 6;
      else if (basepixels < 10)
        avgpixels = 8;
      else if (basepixels < 12)
        avgpixels = 10;
      else if (basepixels < 15)
        avgpixels = 12;
      else
        avgpixels = 15;
  DBG(DBG_info, "%s: pixels_per_line=%d,  coeff=0x%04x,  averaging over %d pixels\n", __func__,
      pixels_per_line, coeff, avgpixels);

  for (x = 0; x <= pixels_per_line - avgpixels; x += avgpixels) {
    memset (&br_tmp, 0, sizeof(br_tmp));
    memset (&dk_tmp, 0, sizeof(dk_tmp));

    for (i = 0; i < avgpixels; i++) {
      for (j = 0; j < channels; j++) {
                br_tmp[j] += dev->white_average_data[((x + i) * channels + j)];
                dk_tmp[i] += dev->dark_average_data[((x + i) * channels + j)];
      }
    }
    for (j = 0; j < channels; j++) {
      br_tmp[j] /= avgpixels;
      dk_tmp[j] /= avgpixels;

      if (br_tmp[j] * target_dark > dk_tmp[j] * target_bright)
        val1 = 0;
      else if (dk_tmp[j] * target_bright - br_tmp[j] * target_dark > 65535 * (target_bright - target_dark))
        val1 = 65535;
      else
        val1 = (dk_tmp[j] * target_bright - br_tmp[j] * target_dark) / (target_bright - target_dark);

      val2 = br_tmp[j] - dk_tmp[j];
      if (65535 * val2 > (target_bright - target_dark) * coeff)
        val2 = (coeff * (target_bright - target_dark)) / val2;
      else
        val2 = 65535;

      br_tmp[j] = val1;
      dk_tmp[j] = val2;
    }
    for (i = 0; i < avgpixels; i++) {
      for (j = 0; j < channels; j++) {
        * ptr++ = br_tmp[ cmat[j] ] & 0xff;
        * ptr++ = br_tmp[ cmat[j] ] >> 8;
        * ptr++ = dk_tmp[ cmat[j] ] & 0xff;
        * ptr++ = dk_tmp[ cmat[j] ] >> 8;
        patch_cnt++;
        if (patch_cnt == patch_size) {
          patch_cnt = 0;
          val1 = cmat[2];
          cmat[2] = cmat[1];
          cmat[1] = cmat[0];
          cmat[0] = val1;
        }
      }
    }
  }
}

static void genesys_send_shading_coefficient(Genesys_Device* dev, const Genesys_Sensor& sensor)
{
    DBG_HELPER(dbg);

    if (sensor.use_host_side_calib) {
        return;
    }

  uint32_t pixels_per_line;
  int o;
  unsigned int length;		/**> number of shading calibration data words */
  unsigned int factor;
  unsigned int coeff, target_code, words_per_color = 0;


    // BUG: we are using wrong pixel number here
    unsigned start_offset =
            dev->calib_session.params.startx * sensor.optical_res / dev->calib_session.params.xres;

    if (dev->model->asic_type == AsicType::GL843) {
        pixels_per_line = dev->calib_session.output_pixels + start_offset;
    } else {
        pixels_per_line = dev->calib_session.params.pixels + start_offset;
    }

    unsigned channels = dev->calib_session.params.channels;

  /* we always build data for three channels, even for gray
   * we make the shading data such that each color channel data line is contiguous
   * to the next one, which allow to write the 3 channels in 1 write
   * during genesys_send_shading_coefficient, some values are words, other bytes
   * hence the x2 factor */
  switch (dev->reg.get8(0x05) >> 6)
    {
      /* 600 dpi */
    case 0:
      words_per_color = 0x2a00;
      break;
      /* 1200 dpi */
    case 1:
      words_per_color = 0x5500;
      break;
      /* 2400 dpi */
    case 2:
      words_per_color = 0xa800;
      break;
      /* 4800 dpi */
    case 3:
      words_per_color = 0x15000;
      break;
    }

  /* special case, memory is aligned on 0x5400, this has yet to be explained */
  /* could be 0xa800 because sensor is truly 2400 dpi, then halved because
   * we only set 1200 dpi */
  if(dev->model->sensor_id==SensorId::CIS_CANON_LIDE_80)
    {
      words_per_color = 0x5400;
    }

  length = words_per_color * 3 * 2;

  /* allocate computed size */
  // contains 16bit words in little endian
  std::vector<uint8_t> shading_data(length, 0);

    if (!dev->calib_session.computed) {
        genesys_send_offset_and_shading(dev, sensor, shading_data.data(), length);
        return;
    }

  /* TARGET/(Wn-Dn) = white gain -> ~1.xxx then it is multiplied by 0x2000
     or 0x4000 to give an integer
     Wn = white average for column n
     Dn = dark average for column n
   */
    if (get_registers_gain4_bit(dev->model->asic_type, dev->reg)) {
        coeff = 0x4000;
    } else {
        coeff = 0x2000;
    }

  /* compute avg factor */
  if(dev->settings.xres>sensor.optical_res)
    {
      factor=1;
    }
  else
    {
      factor=sensor.optical_res/dev->settings.xres;
    }

  /* for GL646, shading data is planar if REG_0x01_FASTMOD is set and
   * chunky if not. For now we rely on the fact that we know that
   * each sensor is used only in one mode. Currently only the CIS_XP200
   * sets REG_0x01_FASTMOD.
   */

  /* TODO setup a struct in genesys_devices that
   * will handle these settings instead of having this switch growing up */
  switch (dev->model->sensor_id)
    {
    case SensorId::CCD_XP300:
    case SensorId::CCD_ROADWARRIOR:
    case SensorId::CCD_DP665:
    case SensorId::CCD_DP685:
    case SensorId::CCD_DSMOBILE600:
      target_code = 0xdc00;
      o = 4;
      compute_planar_coefficients (dev,
                   shading_data.data(),
				   factor,
				   pixels_per_line,
				   words_per_color,
				   channels,
                   ColorOrder::RGB,
				   o,
				   coeff,
				   target_code);
      break;
    case SensorId::CIS_XP200:
      target_code = 0xdc00;
      o = 2;
      compute_planar_coefficients (dev,
                   shading_data.data(),
				   1,
				   pixels_per_line,
				   words_per_color,
				   channels,
                   ColorOrder::GBR,
				   o,
				   coeff,
				   target_code);
      break;
    case SensorId::CCD_HP2300:
      target_code = 0xdc00;
      o = 2;
      if(dev->settings.xres<=sensor.optical_res/2)
       {
          o = o - sensor.dummy_pixel / 2;
       }
      compute_coefficients (dev,
                shading_data.data(),
			    pixels_per_line,
			    3,
                            ColorOrder::RGB,
                            o,
                            coeff,
                            target_code);
      break;
    case SensorId::CCD_5345:
      target_code = 0xe000;
      o = 4;
      if(dev->settings.xres<=sensor.optical_res/2)
       {
          o = o - sensor.dummy_pixel;
       }
      compute_coefficients (dev,
                shading_data.data(),
			    pixels_per_line,
			    3,
                            ColorOrder::RGB,
                            o,
                            coeff,
                            target_code);
      break;
    case SensorId::CCD_HP3670:
    case SensorId::CCD_HP2400:
      target_code = 0xe000;
            // offset is dependent on ccd_pixels_per_system_pixel(), but we couldn't use this in
            // common code previously.
            // FIXME: use sensor.ccd_pixels_per_system_pixel()
      if(dev->settings.xres<=300)
        {
                o = -10;
        }
      else if(dev->settings.xres<=600)
        {
                o = -6;
        }
      else
        {
          o = +2;
        }
      compute_coefficients (dev,
                shading_data.data(),
			    pixels_per_line,
			    3,
                            ColorOrder::RGB,
                            o,
                            coeff,
                            target_code);
      break;
    case SensorId::CCD_KVSS080:
    case SensorId::CCD_PLUSTEK_OPTICBOOK_3800:
    case SensorId::CCD_G4050:
        case SensorId::CCD_HP_4850C:
    case SensorId::CCD_CANON_4400F:
    case SensorId::CCD_CANON_8400F:
    case SensorId::CCD_CANON_8600F:
    case SensorId::CCD_PLUSTEK_OPTICFILM_7200I:
    case SensorId::CCD_PLUSTEK_OPTICFILM_7300:
    case SensorId::CCD_PLUSTEK_OPTICFILM_7500I:
      target_code = 0xe000;
      o = 0;
      compute_coefficients (dev,
                shading_data.data(),
			    pixels_per_line,
			    3,
                            ColorOrder::RGB,
                            o,
                            coeff,
                            target_code);
      break;
    case SensorId::CIS_CANON_LIDE_700F:
    case SensorId::CIS_CANON_LIDE_100:
    case SensorId::CIS_CANON_LIDE_200:
    case SensorId::CIS_CANON_LIDE_110:
    case SensorId::CIS_CANON_LIDE_120:
    case SensorId::CIS_CANON_LIDE_210:
    case SensorId::CIS_CANON_LIDE_220:
        /* TODO store this in a data struct so we avoid
         * growing this switch */
        switch(dev->model->sensor_id)
          {
          case SensorId::CIS_CANON_LIDE_110:
          case SensorId::CIS_CANON_LIDE_120:
          case SensorId::CIS_CANON_LIDE_210:
          case SensorId::CIS_CANON_LIDE_220:
          case SensorId::CIS_CANON_LIDE_700F:
                target_code = 0xc000;
            break;
          default:
            target_code = 0xdc00;
          }
        words_per_color=pixels_per_line*2;
        length = words_per_color * 3 * 2;
        shading_data.clear();
        shading_data.resize(length, 0);
        compute_planar_coefficients (dev,
                                     shading_data.data(),
                                     1,
                                     pixels_per_line,
                                     words_per_color,
                                     channels,
                                     ColorOrder::RGB,
                                     0,
                                     coeff,
                                     target_code);
      break;
    case SensorId::CIS_CANON_LIDE_35:
      compute_averaged_planar (dev, sensor,
                               shading_data.data(),
                               pixels_per_line,
                               words_per_color,
                               channels,
                               4,
                               coeff,
                               0xe000,
                               0x0a00);
      break;
    case SensorId::CIS_CANON_LIDE_80:
      compute_averaged_planar (dev, sensor,
                               shading_data.data(),
                               pixels_per_line,
                               words_per_color,
                               channels,
                               0,
                               coeff,
			       0xe000,
                               0x0800);
      break;
    case SensorId::CCD_PLUSTEK_OPTICPRO_3600:
      compute_shifted_coefficients (dev, sensor,
                        shading_data.data(),
			            pixels_per_line,
			            channels,
                        ColorOrder::RGB,
			            12,         /* offset */
			            coeff,
 			            0x0001,      /* target_dark */
			            0xf900,      /* target_bright */
			            256);        /* patch_size: contigous extent */
      break;
    default:
        throw SaneException(SANE_STATUS_UNSUPPORTED, "sensor %d not supported",
                            static_cast<unsigned>(dev->model->sensor_id));
      break;
    }

    // do the actual write of shading calibration data to the scanner
    genesys_send_offset_and_shading(dev, sensor, shading_data.data(), length);
}


/**
 * search calibration cache list for an entry matching required scan.
 * If one is found, set device calibration with it
 * @param dev scanner's device
 * @return false if no matching cache entry has been
 * found, true if one has been found and used.
 */
static bool
genesys_restore_calibration(Genesys_Device * dev, Genesys_Sensor& sensor)
{
    DBG_HELPER(dbg);

    // if no cache or no function to evaluate cache entry ther can be no match/
    if (dev->calibration_cache.empty()) {
        return false;
    }

    auto session = dev->cmd_set->calculate_scan_session(dev, sensor, dev->settings);

  /* we walk the link list of calibration cache in search for a
   * matching one */
  for (auto& cache : dev->calibration_cache)
    {
        if (sanei_genesys_is_compatible_calibration(dev, session, &cache, false)) {
            dev->frontend = cache.frontend;
          /* we don't restore the gamma fields */
          sensor.exposure = cache.sensor.exposure;

            dev->calib_session = cache.session;
          dev->average_size = cache.average_size;

          dev->dark_average_data = cache.dark_average_data;
          dev->white_average_data = cache.white_average_data;

            if (!dev->cmd_set->has_send_shading_data()) {
            genesys_send_shading_coefficient(dev, sensor);
          }

          DBG(DBG_proc, "%s: restored\n", __func__);
          return true;
	}
    }
  DBG(DBG_proc, "%s: completed(nothing found)\n", __func__);
  return false;
}


static void genesys_save_calibration(Genesys_Device* dev, const Genesys_Sensor& sensor)
{
    DBG_HELPER(dbg);
#ifdef HAVE_SYS_TIME_H
  struct timeval time;
#endif

    auto session = dev->cmd_set->calculate_scan_session(dev, sensor, dev->settings);

  auto found_cache_it = dev->calibration_cache.end();
  for (auto cache_it = dev->calibration_cache.begin(); cache_it != dev->calibration_cache.end();
       cache_it++)
    {
        if (sanei_genesys_is_compatible_calibration(dev, session, &*cache_it, true)) {
            found_cache_it = cache_it;
            break;
        }
    }

  /* if we found on overridable cache, we reuse it */
  if (found_cache_it == dev->calibration_cache.end())
    {
      /* create a new cache entry and insert it in the linked list */
      dev->calibration_cache.push_back(Genesys_Calibration_Cache());
      found_cache_it = std::prev(dev->calibration_cache.end());
    }

  found_cache_it->average_size = dev->average_size;

  found_cache_it->dark_average_data = dev->dark_average_data;
  found_cache_it->white_average_data = dev->white_average_data;

    found_cache_it->params = session.params;
  found_cache_it->frontend = dev->frontend;
  found_cache_it->sensor = sensor;

    found_cache_it->session = dev->calib_session;

#ifdef HAVE_SYS_TIME_H
    gettimeofday(&time, nullptr);
  found_cache_it->last_calibration = time.tv_sec;
#endif
}

/**
 * does the calibration process for a flatbed scanner
 * - offset calibration
 * - gain calibration
 * - shading calibration
 * @param dev device to calibrate
 */
static void genesys_flatbed_calibration(Genesys_Device* dev, Genesys_Sensor& sensor)
{
    DBG_HELPER(dbg);
  uint32_t pixels_per_line;

    unsigned coarse_res = sensor.optical_res;
    if (dev->settings.yres <= sensor.optical_res / 2) {
        coarse_res /= 2;
    }

    if (dev->model->model_id == ModelId::CANON_8400F) {
        coarse_res = 1600;
    }

    if (dev->model->model_id == ModelId::CANON_4400F ||
        dev->model->model_id == ModelId::CANON_8600F)
    {
        coarse_res = 1200;
    }

    auto local_reg = dev->initial_regs;

    // do offset calibration if needed
    dev->interface->record_progress_message("offset_calibration");
    dev->cmd_set->offset_calibration(dev, sensor, local_reg);

    dev->interface->record_progress_message("coarse_gain_calibration");
    dev->cmd_set->coarse_gain_calibration(dev, sensor, local_reg, coarse_res);

  if (dev->model->is_cis)
    {
      /* the afe now sends valid data for doing led calibration */
        dev->interface->record_progress_message("led_calibration");
        switch (dev->model->asic_type) {
            case AsicType::GL124:
            case AsicType::GL845:
            case AsicType::GL846:
            case AsicType::GL847: {
                auto calib_exposure = dev->cmd_set->led_calibration(dev, sensor, local_reg);
                for (auto& sensor_update :
                        sanei_genesys_find_sensors_all_for_write(dev, sensor.method)) {
                    sensor_update.get().exposure = calib_exposure;
                }
                sensor.exposure = calib_exposure;
                break;
            }
            default: {
                sensor.exposure = dev->cmd_set->led_calibration(dev, sensor, local_reg);
            }
        }


        // calibrate afe again to match new exposure
        dev->interface->record_progress_message("offset_calibration");
        dev->cmd_set->offset_calibration(dev, sensor, local_reg);

        dev->interface->record_progress_message("coarse_gain_calibration");
        dev->cmd_set->coarse_gain_calibration(dev, sensor, local_reg, coarse_res);
    }

  /* we always use sensor pixel number when the ASIC can't handle multi-segments sensor */
    if (!has_flag(dev->model->flags, ModelFlag::SIS_SENSOR)) {
        pixels_per_line = static_cast<std::uint32_t>((dev->model->x_size * dev->settings.xres) /
                                                     MM_PER_INCH);
    } else {
        pixels_per_line = static_cast<std::uint32_t>((dev->model->x_size_calib_mm * dev->settings.xres)
                                                      / MM_PER_INCH);
    }

    // send default shading data
    dev->interface->record_progress_message("sanei_genesys_init_shading_data");
    sanei_genesys_init_shading_data(dev, sensor, pixels_per_line);

  if (dev->settings.scan_method == ScanMethod::TRANSPARENCY ||
      dev->settings.scan_method == ScanMethod::TRANSPARENCY_INFRARED)
  {
        dev->cmd_set->move_to_ta(dev);
  }

    // shading calibration
    if (has_flag(dev->model->flags, ModelFlag::DARK_WHITE_CALIBRATION)) {
        dev->interface->record_progress_message("genesys_dark_white_shading_calibration");
        genesys_dark_white_shading_calibration(dev, sensor, local_reg);
    } else {
        DBG(DBG_proc, "%s : genesys_dark_shading_calibration local_reg ", __func__);
        debug_dump(DBG_proc, local_reg);

        if (has_flag(dev->model->flags, ModelFlag::DARK_CALIBRATION)) {
            dev->interface->record_progress_message("genesys_dark_shading_calibration");
            genesys_dark_shading_calibration(dev, sensor, local_reg);
            genesys_repark_sensor_before_shading(dev);
        }

        dev->interface->record_progress_message("genesys_white_shading_calibration");
        genesys_white_shading_calibration(dev, sensor, local_reg);

        genesys_repark_sensor_after_white_shading(dev);

        if (!has_flag(dev->model->flags, ModelFlag::DARK_CALIBRATION)) {
            genesys_dummy_dark_shading(dev, sensor);
        }
    }

    if (!dev->cmd_set->has_send_shading_data()) {
        dev->interface->record_progress_message("genesys_send_shading_coefficient");
        genesys_send_shading_coefficient(dev, sensor);
    }
}

/**
 * Does the calibration process for a sheetfed scanner
 * - offset calibration
 * - gain calibration
 * - shading calibration
 * During calibration a predefined calibration sheet with specific black and white
 * areas is used.
 * @param dev device to calibrate
 */
static void genesys_sheetfed_calibration(Genesys_Device* dev, Genesys_Sensor& sensor)
{
    DBG_HELPER(dbg);
    bool forward = true;

    auto local_reg = dev->initial_regs;

    // first step, load document
    dev->cmd_set->load_document(dev);

  /* led, offset and gain calibration are influenced by scan
   * settings. So we set it to sensor resolution */
  dev->settings.xres = sensor.optical_res;
  /* XP200 needs to calibrate a full and half sensor's resolution */
    if (dev->model->sensor_id == SensorId::CIS_XP200 &&
        dev->settings.xres <= sensor.optical_res / 2)
    {
        dev->settings.xres /= 2;
    }

  /* the afe needs to sends valid data even before calibration */

  /* go to a white area */
    try {
        scanner_search_strip(*dev, forward, false);
    } catch (...) {
        catch_all_exceptions(__func__, [&](){ dev->cmd_set->eject_document(dev); });
        throw;
    }

  if (dev->model->is_cis)
    {
        dev->cmd_set->led_calibration(dev, sensor, local_reg);
    }

    dev->cmd_set->offset_calibration(dev, sensor, local_reg);

    dev->cmd_set->coarse_gain_calibration(dev, sensor, local_reg, sensor.optical_res);

  /* search for a full width black strip and then do a 16 bit scan to
   * gather black shading data */
    if (has_flag(dev->model->flags, ModelFlag::DARK_CALIBRATION)) {
        // seek black/white reverse/forward
        try {
            scanner_search_strip(*dev, forward, true);
        } catch (...) {
            catch_all_exceptions(__func__, [&](){ dev->cmd_set->eject_document(dev); });
            throw;
        }

        try {
            genesys_dark_shading_calibration(dev, sensor, local_reg);
        } catch (...) {
            catch_all_exceptions(__func__, [&](){ dev->cmd_set->eject_document(dev); });
            throw;
        }
        forward = false;
    }


  /* go to a white area */
    try {
        scanner_search_strip(*dev, forward, false);
    } catch (...) {
        catch_all_exceptions(__func__, [&](){ dev->cmd_set->eject_document(dev); });
        throw;
    }

  genesys_repark_sensor_before_shading(dev);

    try {
        genesys_white_shading_calibration(dev, sensor, local_reg);
        genesys_repark_sensor_after_white_shading(dev);
    } catch (...) {
        catch_all_exceptions(__func__, [&](){ dev->cmd_set->eject_document(dev); });
        throw;
    }

    // in case we haven't black shading data, build it from black pixels of white calibration
    // FIXME: shouldn't we use genesys_dummy_dark_shading() ?
    if (!has_flag(dev->model->flags, ModelFlag::DARK_CALIBRATION)) {
        dev->dark_average_data.clear();
        dev->dark_average_data.resize(dev->average_size, 0x0f0f);
      /* XXX STEF XXX
       * with black point in white shading, build an average black
       * pixel and use it to fill the dark_average
       * dev->calib_pixels
       (sensor.x_size_calib_mm * dev->settings.xres) / MM_PER_INCH,
       dev->calib_lines,
       */
    }

  /* send the shading coefficient when doing whole line shading
   * but not when using SHDAREA like GL124 */
    if (!dev->cmd_set->has_send_shading_data()) {
        genesys_send_shading_coefficient(dev, sensor);
    }

    // save the calibration data
    genesys_save_calibration(dev, sensor);

    // and finally eject calibration sheet
    dev->cmd_set->eject_document(dev);

    // restore settings
    dev->settings.xres = sensor.optical_res;
}

/**
 * does the calibration process for a device
 * @param dev device to calibrate
 */
static void genesys_scanner_calibration(Genesys_Device* dev, Genesys_Sensor& sensor)
{
    DBG_HELPER(dbg);
    if (!dev->model->is_sheetfed) {
        genesys_flatbed_calibration(dev, sensor);
        return;
    }
    genesys_sheetfed_calibration(dev, sensor);
}


/* ------------------------------------------------------------------------ */
/*                  High level (exported) functions                         */
/* ------------------------------------------------------------------------ */

/*
 * wait lamp to be warm enough by scanning the same line until
 * differences between two scans are below a threshold
 */
static void genesys_warmup_lamp(Genesys_Device* dev)
{
    DBG_HELPER(dbg);
    unsigned seconds = 0;
  int pixel;
  int channels, total_size;
  double first_average = 0;
  double second_average = 0;
  int difference = 255;
    int lines = 3;

  const auto& sensor = sanei_genesys_find_sensor_any(dev);

    dev->cmd_set->init_regs_for_warmup(dev, sensor, &dev->reg, &channels, &total_size);
    dev->interface->write_registers(dev->reg);

  std::vector<uint8_t> first_line(total_size);
  std::vector<uint8_t> second_line(total_size);

  do
    {
      DBG(DBG_info, "%s: one more loop\n", __func__);
        dev->cmd_set->begin_scan(dev, sensor, &dev->reg, false);

        if (is_testing_mode()) {
            dev->interface->test_checkpoint("warmup_lamp");
            dev->cmd_set->end_scan(dev, &dev->reg, true);
            return;
        }

        wait_until_buffer_non_empty(dev);

        try {
            sanei_genesys_read_data_from_scanner(dev, first_line.data(), total_size);
        } catch (...) {
            // FIXME: document why this retry is here
            sanei_genesys_read_data_from_scanner(dev, first_line.data(), total_size);
        }

        dev->cmd_set->end_scan(dev, &dev->reg, true);

        dev->interface->sleep_ms(1000);
      seconds++;

        dev->cmd_set->begin_scan(dev, sensor, &dev->reg, false);

        wait_until_buffer_non_empty(dev);

        sanei_genesys_read_data_from_scanner(dev, second_line.data(), total_size);
        dev->cmd_set->end_scan(dev, &dev->reg, true);

      /* compute difference between the two scans */
      for (pixel = 0; pixel < total_size; pixel++)
	{
            // 16 bit data
            if (dev->session.params.depth == 16) {
	      first_average += (first_line[pixel] + first_line[pixel + 1] * 256);
	      second_average += (second_line[pixel] + second_line[pixel + 1] * 256);
	      pixel++;
	    }
	  else
	    {
	      first_average += first_line[pixel];
	      second_average += second_line[pixel];
	    }
	}
        if (dev->session.params.depth == 16) {
	  first_average /= pixel;
	  second_average /= pixel;
            difference = static_cast<int>(std::fabs(first_average - second_average));
	  DBG(DBG_info, "%s: average = %.2f, diff = %.3f\n", __func__,
	      100 * ((second_average) / (256 * 256)),
	      100 * (difference / second_average));

	  if (second_average > (100 * 256)
	      && (difference / second_average) < 0.002)
	    break;
	}
      else
	{
	  first_average /= pixel;
	  second_average /= pixel;
	  if (DBG_LEVEL >= DBG_data)
	    {
              sanei_genesys_write_pnm_file("gl_warmup1.pnm", first_line.data(), 8, channels,
                                           total_size / (lines * channels), lines);
              sanei_genesys_write_pnm_file("gl_warmup2.pnm", second_line.data(), 8, channels,
                                           total_size / (lines * channels), lines);
	    }
	  DBG(DBG_info, "%s: average 1 = %.2f, average 2 = %.2f\n", __func__, first_average,
	      second_average);
          /* if delta below 15/255 ~= 5.8%, lamp is considred warm enough */
	  if (fabs (first_average - second_average) < 15
	      && second_average > 55)
	    break;
	}

      /* sleep another second before next loop */
        dev->interface->sleep_ms(1000);
        seconds++;
    } while (seconds < WARMUP_TIME);

  if (seconds >= WARMUP_TIME)
    {
        throw SaneException(SANE_STATUS_IO_ERROR,
                            "warmup timed out after %d seconds. Lamp defective?", seconds);
    }
  else
    {
      DBG(DBG_info, "%s: warmup succeeded after %d seconds\n", __func__, seconds);
    }
}


// High-level start of scanning
static void genesys_start_scan(Genesys_Device* dev, bool lamp_off)
{
    DBG_HELPER(dbg);
  unsigned int steps, expected;

  /* since not all scanners are set ot wait for head to park
   * we check we are not still parking before starting a new scan */
    if (dev->parking) {
        sanei_genesys_wait_for_home(dev);
    }

    // disable power saving
    dev->cmd_set->save_power(dev, false);

  /* wait for lamp warmup : until a warmup for TRANSPARENCY is designed, skip
   * it when scanning from XPA. */
    if (!has_flag(dev->model->flags, ModelFlag::SKIP_WARMUP) &&
        (dev->settings.scan_method == ScanMethod::FLATBED))
    {
        genesys_warmup_lamp(dev);
    }

  /* set top left x and y values by scanning the internals if flatbed scanners */
    if (!dev->model->is_sheetfed) {
        // TODO: check we can drop this since we cannot have the scanner's head wandering here
        dev->parking = false;
        dev->cmd_set->move_back_home(dev, true);
    }

  /* move to calibration area for transparency adapter */
    if (dev->settings.scan_method == ScanMethod::TRANSPARENCY ||
        dev->settings.scan_method == ScanMethod::TRANSPARENCY_INFRARED)
    {
        dev->cmd_set->move_to_ta(dev);
    }

  /* load document if needed (for sheetfed scanner for instance) */
    if (dev->model->is_sheetfed) {
        dev->cmd_set->load_document(dev);
    }

    auto& sensor = sanei_genesys_find_sensor_for_write(dev, dev->settings.xres,
                                                       dev->settings.get_channels(),
                                                       dev->settings.scan_method);

    // send gamma tables. They have been set to device or user value
    // when setting option value */
    dev->cmd_set->send_gamma_table(dev, sensor);

  /* try to use cached calibration first */
  if (!genesys_restore_calibration (dev, sensor))
    {
       /* calibration : sheetfed scanners can't calibrate before each scan */
       /* and also those who have the NO_CALIBRATION flag                  */
        if (!has_flag(dev->model->flags, ModelFlag::NO_CALIBRATION) && !dev->model->is_sheetfed) {
            genesys_scanner_calibration(dev, sensor);
          genesys_save_calibration (dev, sensor);
	}
      else
	{
          DBG(DBG_warn, "%s: no calibration done\n", __func__);
	}
    }

  /* build look up table for dynamic lineart */
    if (dev->settings.scan_mode == ScanColorMode::LINEART) {
        sanei_genesys_load_lut(dev->lineart_lut, 8, 8, 50, 205, dev->settings.threshold_curve,
                               dev->settings.threshold-127);
    }

    dev->cmd_set->wait_for_motor_stop(dev);

    if (dev->cmd_set->needs_home_before_init_regs_for_scan(dev)) {
        dev->cmd_set->move_back_home(dev, true);
    }

    if (dev->settings.scan_method == ScanMethod::TRANSPARENCY ||
        dev->settings.scan_method == ScanMethod::TRANSPARENCY_INFRARED)
    {
        dev->cmd_set->move_to_ta(dev);
    }

    dev->cmd_set->init_regs_for_scan(dev, sensor, dev->reg);

  /* no lamp during scan */
    if (lamp_off) {
        sanei_genesys_set_lamp_power(dev, sensor, dev->reg, false);
    }

  /* GL124 is using SHDAREA, so we have to wait for scan to be set up before
   * sending shading data */
    if (dev->cmd_set->has_send_shading_data() &&
        !has_flag(dev->model->flags, ModelFlag::NO_CALIBRATION))
    {
        genesys_send_shading_coefficient(dev, sensor);
    }

    // now send registers for scan
    dev->interface->write_registers(dev->reg);

    // start effective scan
    dev->cmd_set->begin_scan(dev, sensor, &dev->reg, true);

    if (is_testing_mode()) {
        dev->interface->test_checkpoint("start_scan");
        return;
    }

  /*do we really need this? the valid data check should be sufficent -- pierre*/
  /* waits for head to reach scanning position */
  expected = dev->reg.get8(0x3d) * 65536
           + dev->reg.get8(0x3e) * 256
           + dev->reg.get8(0x3f);
  do
    {
        // wait some time between each test to avoid overloading USB and CPU
        dev->interface->sleep_ms(100);
        sanei_genesys_read_feed_steps (dev, &steps);
    }
  while (steps < expected);

    wait_until_buffer_non_empty(dev);

    // we wait for at least one word of valid scan data
    // this is also done in sanei_genesys_read_data_from_scanner -- pierre
    if (!dev->model->is_sheetfed) {
        do {
            dev->interface->sleep_ms(100);
            sanei_genesys_read_valid_words(dev, &steps);
        }
      while (steps < 1);
    }
}

static void genesys_fill_read_buffer(Genesys_Device* dev)
{
    DBG_HELPER(dbg);

  /* for sheetfed scanner, we must check is document is shorter than
   * the requested scan */
    if (dev->model->is_sheetfed) {
        dev->cmd_set->detect_document_end(dev);
    }

    std::size_t size = dev->read_buffer.size() - dev->read_buffer.avail();

  /* due to sensors and motors, not all data can be directly used. It
   * may have to be read from another intermediate buffer and then processed.
   * There are currently 3 intermediate stages:
   * - handling of odd/even sensors
   * - handling of line interpolation for motors that can't have low
   *   enough dpi
   * - handling of multi-segments sensors
   *
   * This is also the place where full duplex data will be handled.
   */
    dev->pipeline_buffer.get_data(size, dev->read_buffer.get_write_pos(size));

    dev->read_buffer.produce(size);
}

/* this function does the effective data read in a manner that suits
   the scanner. It does data reordering and resizing if need.
   It also manages EOF and I/O errors, and line distance correction.
    Returns true on success, false on end-of-file.
*/
static void genesys_read_ordered_data(Genesys_Device* dev, SANE_Byte* destination, size_t* len)
{
    DBG_HELPER(dbg);
    size_t bytes = 0;
  uint8_t *work_buffer_src;
  Genesys_Buffer *src_buffer;

    if (!dev->read_active) {
      *len = 0;
        throw SaneException("read is not active");
    }

    DBG(DBG_info, "%s: frontend requested %zu bytes\n", __func__, *len);
    DBG(DBG_info, "%s: bytes_to_read=%zu, total_bytes_read=%zu\n", __func__,
        dev->total_bytes_to_read, dev->total_bytes_read);

  /* is there data left to scan */
  if (dev->total_bytes_read >= dev->total_bytes_to_read)
    {
      /* issue park command immediatly in case scanner can handle it
       * so we save time */
        if (!dev->model->is_sheetfed && !has_flag(dev->model->flags, ModelFlag::MUST_WAIT) &&
            !dev->parking)
        {
            dev->cmd_set->move_back_home(dev, false);
            dev->parking = true;
        }
        throw SaneException(SANE_STATUS_EOF, "nothing more to scan: EOF");
    }

/* convert data */
/*
  0. fill_read_buffer
-------------- read_buffer ----------------------
  1a). (opt)uncis                    (assumes color components to be laid out
                                    planar)
  1b). (opt)reverse_RGB              (assumes pixels to be BGR or BBGGRR))
-------------- lines_buffer ----------------------
  2a). (opt)line_distance_correction (assumes RGB or RRGGBB)
  2b). (opt)unstagger                (assumes pixels to be depth*channels/8
                                      bytes long, unshrinked)
------------- shrink_buffer ---------------------
  3. (opt)shrink_lines             (assumes component separation in pixels)
-------------- out_buffer -----------------------
  4. memcpy to destination (for lineart with bit reversal)
*/
/*FIXME: for lineart we need sub byte addressing in buffers, or conversion to
  bytes at 0. and back to bits at 4.
Problems with the first approach:
  - its not clear how to check if we need to output an incomplete byte
    because it is the last one.
 */
/*FIXME: add lineart support for gl646. in the meantime add logic to convert
  from gray to lineart at the end? would suffer the above problem,
  total_bytes_to_read and total_bytes_read help in that case.
 */

    if (is_testing_mode()) {
        if (dev->total_bytes_read + *len > dev->total_bytes_to_read) {
            *len = dev->total_bytes_to_read - dev->total_bytes_read;
        }
        dev->total_bytes_read += *len;
    } else {
        genesys_fill_read_buffer(dev);

        src_buffer = &(dev->read_buffer);

        /* move data to destination */
        bytes = std::min(src_buffer->avail(), *len);

        work_buffer_src = src_buffer->get_read_pos();

        std::memcpy(destination, work_buffer_src, bytes);
        *len = bytes;

        /* avoid signaling some extra data because we have treated a full block
        * on the last block */
        if (dev->total_bytes_read + *len > dev->total_bytes_to_read) {
            *len = dev->total_bytes_to_read - dev->total_bytes_read;
        }

        /* count bytes sent to frontend */
        dev->total_bytes_read += *len;

        src_buffer->consume(bytes);
    }

  /* end scan if all needed data have been read */
   if(dev->total_bytes_read >= dev->total_bytes_to_read)
    {
        dev->cmd_set->end_scan(dev, &dev->reg, true);
        if (dev->model->is_sheetfed) {
            dev->cmd_set->eject_document (dev);
        }
    }

    DBG(DBG_proc, "%s: completed, %zu bytes read\n", __func__, bytes);
}



/* ------------------------------------------------------------------------ */
/*                  Start of higher level functions                         */
/* ------------------------------------------------------------------------ */

static size_t
max_string_size (const SANE_String_Const strings[])
{
  size_t size, max_size = 0;
  SANE_Int i;

  for (i = 0; strings[i]; ++i)
    {
      size = strlen (strings[i]) + 1;
      if (size > max_size)
	max_size = size;
    }
  return max_size;
}

static std::size_t max_string_size(const std::vector<const char*>& strings)
{
    std::size_t max_size = 0;
    for (const auto& s : strings) {
        if (!s) {
            continue;
        }
        max_size = std::max(max_size, std::strlen(s));
    }
    return max_size;
}

static unsigned pick_resolution(const std::vector<unsigned>& resolutions, unsigned resolution,
                                const char* direction)
{
    DBG_HELPER(dbg);

    if (resolutions.empty())
        throw SaneException("Empty resolution list");

    unsigned best_res = resolutions.front();
    unsigned min_diff = abs_diff(best_res, resolution);

    for (auto it = std::next(resolutions.begin()); it != resolutions.end(); ++it) {
        unsigned curr_diff = abs_diff(*it, resolution);
        if (curr_diff < min_diff) {
            min_diff = curr_diff;
            best_res = *it;
        }
    }

    if (best_res != resolution) {
        DBG(DBG_warn, "%s: using resolution %d that is nearest to %d for direction %s\n",
            __func__, best_res, resolution, direction);
    }
    return best_res;
}

static void calc_parameters(Genesys_Scanner* s)
{
    DBG_HELPER(dbg);
    float tl_x = 0, tl_y = 0, br_x = 0, br_y = 0;

    tl_x = fixed_to_float(s->pos_top_left_x);
    tl_y = fixed_to_float(s->pos_top_left_y);
    br_x = fixed_to_float(s->pos_bottom_right_x);
    br_y = fixed_to_float(s->pos_bottom_right_y);

    s->params.last_frame = true;	/* only single pass scanning supported */

    if (s->mode == SANE_VALUE_SCAN_MODE_GRAY || s->mode == SANE_VALUE_SCAN_MODE_LINEART) {
        s->params.format = SANE_FRAME_GRAY;
    } else {
        s->params.format = SANE_FRAME_RGB;
    }

    if (s->mode == SANE_VALUE_SCAN_MODE_LINEART) {
        s->params.depth = 1;
    } else {
        s->params.depth = s->bit_depth;
    }

    s->dev->settings.scan_method = s->scan_method;
    const auto& resolutions = s->dev->model->get_resolution_settings(s->dev->settings.scan_method);

    s->dev->settings.depth = s->bit_depth;

  /* interpolation */
  s->dev->settings.disable_interpolation = s->disable_interpolation;

  // FIXME: use correct sensor
  const auto& sensor = sanei_genesys_find_sensor_any(s->dev);

    // hardware settings
    if (static_cast<unsigned>(s->resolution) > sensor.optical_res &&
        s->dev->settings.disable_interpolation)
    {
        s->dev->settings.xres = sensor.optical_res;
    } else {
        s->dev->settings.xres = s->resolution;
    }
    s->dev->settings.yres = s->resolution;

    s->dev->settings.xres = pick_resolution(resolutions.resolutions_x, s->dev->settings.xres, "X");
    s->dev->settings.yres = pick_resolution(resolutions.resolutions_y, s->dev->settings.yres, "Y");

    s->params.lines = static_cast<unsigned>(((br_y - tl_y) * s->dev->settings.yres) /
                                            MM_PER_INCH);
    unsigned pixels_per_line = static_cast<unsigned>(((br_x - tl_x) * s->dev->settings.xres) /
                                                     MM_PER_INCH);

  /* we need an even pixels number
   * TODO invert test logic or generalize behaviour across all ASICs */
    if (has_flag(s->dev->model->flags, ModelFlag::SIS_SENSOR) ||
        s->dev->model->asic_type == AsicType::GL847 ||
        s->dev->model->asic_type == AsicType::GL124 ||
        s->dev->model->asic_type == AsicType::GL845 ||
        s->dev->model->asic_type == AsicType::GL846 ||
        s->dev->model->asic_type == AsicType::GL843)
    {
        if (s->dev->settings.xres <= 1200) {
            pixels_per_line = (pixels_per_line / 4) * 4;
        } else if (s->dev->settings.xres < s->dev->settings.yres) {
            // BUG: this is an artifact of the fact that the resolution was twice as large than
            // the actual resolution when scanning above the supported scanner X resolution
            pixels_per_line = (pixels_per_line / 8) * 8;
        } else {
            pixels_per_line = (pixels_per_line / 16) * 16;
        }
    }

  /* corner case for true lineart for sensor with several segments
   * or when xres is doubled to match yres */
    if (s->dev->settings.xres >= 1200 && (
                s->dev->model->asic_type == AsicType::GL124 ||
                s->dev->model->asic_type == AsicType::GL847 ||
                s->dev->session.params.xres < s->dev->session.params.yres))
    {
        if (s->dev->settings.xres < s->dev->settings.yres) {
            // FIXME: this is an artifact of the fact that the resolution was twice as large than
            // the actual resolution when scanning above the supported scanner X resolution
            pixels_per_line = (pixels_per_line / 8) * 8;
        } else {
            pixels_per_line = (pixels_per_line / 16) * 16;
        }
    }

    unsigned xres_factor = s->resolution / s->dev->settings.xres;

    unsigned bytes_per_line = 0;

  if (s->params.depth > 8)
    {
      s->params.depth = 16;
        bytes_per_line = 2 * pixels_per_line;
    }
  else if (s->params.depth == 1)
    {
        // round down pixel number. This will is lossy operation, at most 7 pixels will be lost
        pixels_per_line = (pixels_per_line / 8) * 8;
        bytes_per_line = pixels_per_line / 8;
    } else {
        bytes_per_line = pixels_per_line;
    }

    if (s->params.format == SANE_FRAME_RGB) {
        bytes_per_line *= 3;
    }

    s->dev->settings.scan_mode = option_string_to_scan_color_mode(s->mode);

  s->dev->settings.lines = s->params.lines;
    s->dev->settings.pixels = pixels_per_line;
    s->dev->settings.requested_pixels = pixels_per_line * xres_factor;
    s->params.pixels_per_line = pixels_per_line * xres_factor;
    s->params.bytes_per_line = bytes_per_line * xres_factor;
  s->dev->settings.tl_x = tl_x;
  s->dev->settings.tl_y = tl_y;

    // threshold setting
    s->dev->settings.threshold = static_cast<int>(2.55f * (fixed_to_float(s->threshold)));

    // color filter
    if (s->color_filter == "Red") {
        s->dev->settings.color_filter = ColorFilter::RED;
    } else if (s->color_filter == "Green") {
        s->dev->settings.color_filter = ColorFilter::GREEN;
    } else if (s->color_filter == "Blue") {
        s->dev->settings.color_filter = ColorFilter::BLUE;
    } else {
        s->dev->settings.color_filter = ColorFilter::NONE;
    }

    // true gray
    if (s->color_filter == "None") {
        s->dev->settings.true_gray = 1;
    } else {
        s->dev->settings.true_gray = 0;
    }

    // threshold curve for dynamic rasterization
    s->dev->settings.threshold_curve = s->threshold_curve;

  /* some digital processing requires the whole picture to be buffered */
  /* no digital processing takes place when doing preview, or when bit depth is
   * higher than 8 bits */
    if ((s->swdespeck || s->swcrop || s->swdeskew || s->swderotate ||(fixed_to_float(s->swskip)>0))
        && (!s->preview)
        && (s->bit_depth <= 8))
    {
        s->dev->buffer_image = true;
    }
  else
    {
        s->dev->buffer_image = false;
    }

  /* brigthness and contrast only for for 8 bit scans */
  if(s->bit_depth <= 8)
    {
      s->dev->settings.contrast = (s->contrast * 127) / 100;
      s->dev->settings.brightness = (s->brightness * 127) / 100;
    }
  else
    {
      s->dev->settings.contrast=0;
      s->dev->settings.brightness=0;
    }

  /* cache expiration time */
   s->dev->settings.expiration_time = s->expiration_time;
}


static void create_bpp_list (Genesys_Scanner * s, const std::vector<unsigned>& bpp)
{
    s->bpp_list[0] = bpp.size();
    std::reverse_copy(bpp.begin(), bpp.end(), s->bpp_list + 1);
}

/** @brief this function initialize a gamma vector based on the ASIC:
 * Set up a default gamma table vector based on device description
 * gl646: 12 or 14 bits gamma table depending on ModelFlag::GAMMA_14BIT
 * gl84x: 16 bits
 * gl12x: 16 bits
 * @param scanner pointer to scanner session to get options
 * @param option option number of the gamma table to set
 */
static void
init_gamma_vector_option (Genesys_Scanner * scanner, int option)
{
  /* the option is inactive until the custom gamma control
   * is enabled */
  scanner->opt[option].type = SANE_TYPE_INT;
  scanner->opt[option].cap |= SANE_CAP_INACTIVE | SANE_CAP_ADVANCED;
  scanner->opt[option].unit = SANE_UNIT_NONE;
  scanner->opt[option].constraint_type = SANE_CONSTRAINT_RANGE;
    if (scanner->dev->model->asic_type == AsicType::GL646) {
        if (has_flag(scanner->dev->model->flags, ModelFlag::GAMMA_14BIT)) {
	  scanner->opt[option].size = 16384 * sizeof (SANE_Word);
	  scanner->opt[option].constraint.range = &u14_range;
	}
      else
	{			/* 12 bits gamma tables */
	  scanner->opt[option].size = 4096 * sizeof (SANE_Word);
	  scanner->opt[option].constraint.range = &u12_range;
	}
    }
  else
    {				/* other asics have 16 bits words gamma table */
      scanner->opt[option].size = 256 * sizeof (SANE_Word);
      scanner->opt[option].constraint.range = &u16_range;
    }
}

/**
 * allocate a geometry range
 * @param size maximum size of the range
 * @return a pointer to a valid range or nullptr
 */
static SANE_Range create_range(float size)
{
    SANE_Range range;
    range.min = float_to_fixed(0.0);
    range.max = float_to_fixed(size);
    range.quant = float_to_fixed(0.0);
    return range;
}

/** @brief generate calibration cache file nam
 * Generates the calibration cache file name to use.
 * Tries to store the chache in $HOME/.sane or
 * then fallbacks to $TMPDIR or TMP. The filename
 * uses the model name if only one scanner is plugged
 * else is uses the device name when several identical
 * scanners are in use.
 * @param currdev current scanner device
 * @return an allocated string containing a file name
 */
static std::string calibration_filename(Genesys_Device *currdev)
{
    std::string ret;
    ret.resize(PATH_MAX);

  char filename[80];
  unsigned int count;
  unsigned int i;

  /* first compute the DIR where we can store cache:
   * 1 - home dir
   * 2 - $TMPDIR
   * 3 - $TMP
   * 4 - tmp dir
   * 5 - temp dir
   * 6 - then resort to current dir
   */
    char* ptr = std::getenv("HOME");
    if (ptr == nullptr) {
        ptr = std::getenv("USERPROFILE");
    }
    if (ptr == nullptr) {
        ptr = std::getenv("TMPDIR");
    }
    if (ptr == nullptr) {
        ptr = std::getenv("TMP");
    }

  /* now choose filename:
   * 1 - if only one scanner, name of the model
   * 2 - if several scanners of the same model, use device name,
   *     replacing special chars
   */
  count=0;
  /* count models of the same names if several scanners attached */
    if(s_devices->size() > 1) {
        for (const auto& dev : *s_devices) {
            if (dev.model->model_id == currdev->model->model_id) {
                count++;
            }
        }
    }
  if(count>1)
    {
        std::snprintf(filename, sizeof(filename), "%s.cal", currdev->file_name.c_str());
      for(i=0;i<strlen(filename);i++)
        {
          if(filename[i]==':'||filename[i]==PATH_SEP)
            {
              filename[i]='_';
            }
        }
    }
  else
    {
      snprintf(filename,sizeof(filename),"%s.cal",currdev->model->name);
    }

  /* build final final name : store dir + filename */
    if (ptr == nullptr) {
        int size = std::snprintf(&ret.front(), ret.size(), "%s", filename);
        ret.resize(size);
    }
  else
    {
        int size = 0;
#ifdef HAVE_MKDIR
        /* make sure .sane directory exists in existing store dir */
        size = std::snprintf(&ret.front(), ret.size(), "%s%c.sane", ptr, PATH_SEP);
        ret.resize(size);
        mkdir(ret.c_str(), 0700);

        ret.resize(PATH_MAX);
#endif
        size = std::snprintf(&ret.front(), ret.size(), "%s%c.sane%c%s",
                             ptr, PATH_SEP, PATH_SEP, filename);
        ret.resize(size);
    }

    DBG(DBG_info, "%s: calibration filename >%s<\n", __func__, ret.c_str());

    return ret;
}

static void set_resolution_option_values(Genesys_Scanner& s, bool reset_resolution_value)
{
    auto resolutions = s.dev->model->get_resolutions(s.scan_method);

    s.opt_resolution_values.resize(resolutions.size() + 1, 0);
    s.opt_resolution_values[0] = resolutions.size();
    std::copy(resolutions.begin(), resolutions.end(), s.opt_resolution_values.begin() + 1);

    s.opt[OPT_RESOLUTION].constraint.word_list = s.opt_resolution_values.data();

    if (reset_resolution_value) {
        s.resolution = *std::min_element(resolutions.begin(), resolutions.end());
    }
}

static void set_xy_range_option_values(Genesys_Scanner& s)
{
    if (s.scan_method == ScanMethod::FLATBED)
    {
        s.opt_x_range = create_range(s.dev->model->x_size);
        s.opt_y_range = create_range(s.dev->model->y_size);
    }
  else
    {
        s.opt_x_range = create_range(s.dev->model->x_size_ta);
        s.opt_y_range = create_range(s.dev->model->y_size_ta);
    }

    s.opt[OPT_TL_X].constraint.range = &s.opt_x_range;
    s.opt[OPT_TL_Y].constraint.range = &s.opt_y_range;
    s.opt[OPT_BR_X].constraint.range = &s.opt_x_range;
    s.opt[OPT_BR_Y].constraint.range = &s.opt_y_range;

    s.pos_top_left_x = 0;
    s.pos_top_left_y = 0;
    s.pos_bottom_right_x = s.opt_x_range.max;
    s.pos_bottom_right_y = s.opt_y_range.max;
}

static void init_options(Genesys_Scanner* s)
{
    DBG_HELPER(dbg);
  SANE_Int option;
  Genesys_Model *model = s->dev->model;

  memset (s->opt, 0, sizeof (s->opt));

  for (option = 0; option < NUM_OPTIONS; ++option)
    {
      s->opt[option].size = sizeof (SANE_Word);
      s->opt[option].cap = SANE_CAP_SOFT_SELECT | SANE_CAP_SOFT_DETECT;
    }
  s->opt[OPT_NUM_OPTS].name = SANE_NAME_NUM_OPTIONS;
  s->opt[OPT_NUM_OPTS].title = SANE_TITLE_NUM_OPTIONS;
  s->opt[OPT_NUM_OPTS].desc = SANE_DESC_NUM_OPTIONS;
  s->opt[OPT_NUM_OPTS].type = SANE_TYPE_INT;
  s->opt[OPT_NUM_OPTS].cap = SANE_CAP_SOFT_DETECT;

  /* "Mode" group: */
  s->opt[OPT_MODE_GROUP].name = "scanmode-group";
  s->opt[OPT_MODE_GROUP].title = SANE_I18N ("Scan Mode");
  s->opt[OPT_MODE_GROUP].desc = "";
  s->opt[OPT_MODE_GROUP].type = SANE_TYPE_GROUP;
  s->opt[OPT_MODE_GROUP].size = 0;
  s->opt[OPT_MODE_GROUP].cap = 0;
  s->opt[OPT_MODE_GROUP].constraint_type = SANE_CONSTRAINT_NONE;

  /* scan mode */
  s->opt[OPT_MODE].name = SANE_NAME_SCAN_MODE;
  s->opt[OPT_MODE].title = SANE_TITLE_SCAN_MODE;
  s->opt[OPT_MODE].desc = SANE_DESC_SCAN_MODE;
  s->opt[OPT_MODE].type = SANE_TYPE_STRING;
  s->opt[OPT_MODE].constraint_type = SANE_CONSTRAINT_STRING_LIST;
  s->opt[OPT_MODE].size = max_string_size (mode_list);
  s->opt[OPT_MODE].constraint.string_list = mode_list;
  s->mode = SANE_VALUE_SCAN_MODE_GRAY;

  /* scan source */
    s->opt_source_values.clear();
    for (const auto& resolution_setting : model->resolutions) {
        for (auto method : resolution_setting.methods) {
            s->opt_source_values.push_back(scan_method_to_option_string(method));
        }
    }
    s->opt_source_values.push_back(nullptr);

  s->opt[OPT_SOURCE].name = SANE_NAME_SCAN_SOURCE;
  s->opt[OPT_SOURCE].title = SANE_TITLE_SCAN_SOURCE;
  s->opt[OPT_SOURCE].desc = SANE_DESC_SCAN_SOURCE;
  s->opt[OPT_SOURCE].type = SANE_TYPE_STRING;
  s->opt[OPT_SOURCE].constraint_type = SANE_CONSTRAINT_STRING_LIST;
    s->opt[OPT_SOURCE].size = max_string_size(s->opt_source_values);
    s->opt[OPT_SOURCE].constraint.string_list = s->opt_source_values.data();
    if (s->opt_source_values.size() < 2) {
        throw SaneException("No scan methods specified for scanner");
    }
    s->scan_method = model->default_method;

  /* preview */
  s->opt[OPT_PREVIEW].name = SANE_NAME_PREVIEW;
  s->opt[OPT_PREVIEW].title = SANE_TITLE_PREVIEW;
  s->opt[OPT_PREVIEW].desc = SANE_DESC_PREVIEW;
  s->opt[OPT_PREVIEW].type = SANE_TYPE_BOOL;
  s->opt[OPT_PREVIEW].unit = SANE_UNIT_NONE;
  s->opt[OPT_PREVIEW].constraint_type = SANE_CONSTRAINT_NONE;
  s->preview = false;

  /* bit depth */
  s->opt[OPT_BIT_DEPTH].name = SANE_NAME_BIT_DEPTH;
  s->opt[OPT_BIT_DEPTH].title = SANE_TITLE_BIT_DEPTH;
  s->opt[OPT_BIT_DEPTH].desc = SANE_DESC_BIT_DEPTH;
  s->opt[OPT_BIT_DEPTH].type = SANE_TYPE_INT;
  s->opt[OPT_BIT_DEPTH].constraint_type = SANE_CONSTRAINT_WORD_LIST;
  s->opt[OPT_BIT_DEPTH].size = sizeof (SANE_Word);
  s->opt[OPT_BIT_DEPTH].constraint.word_list = s->bpp_list;
  create_bpp_list (s, model->bpp_gray_values);
    s->bit_depth = model->bpp_gray_values[0];

    // resolution
  s->opt[OPT_RESOLUTION].name = SANE_NAME_SCAN_RESOLUTION;
  s->opt[OPT_RESOLUTION].title = SANE_TITLE_SCAN_RESOLUTION;
  s->opt[OPT_RESOLUTION].desc = SANE_DESC_SCAN_RESOLUTION;
  s->opt[OPT_RESOLUTION].type = SANE_TYPE_INT;
  s->opt[OPT_RESOLUTION].unit = SANE_UNIT_DPI;
  s->opt[OPT_RESOLUTION].constraint_type = SANE_CONSTRAINT_WORD_LIST;
    set_resolution_option_values(*s, true);

  /* "Geometry" group: */
  s->opt[OPT_GEOMETRY_GROUP].name = SANE_NAME_GEOMETRY;
  s->opt[OPT_GEOMETRY_GROUP].title = SANE_I18N ("Geometry");
  s->opt[OPT_GEOMETRY_GROUP].desc = "";
  s->opt[OPT_GEOMETRY_GROUP].type = SANE_TYPE_GROUP;
  s->opt[OPT_GEOMETRY_GROUP].cap = SANE_CAP_ADVANCED;
  s->opt[OPT_GEOMETRY_GROUP].size = 0;
  s->opt[OPT_GEOMETRY_GROUP].constraint_type = SANE_CONSTRAINT_NONE;

    s->opt_x_range = create_range(model->x_size);
    s->opt_y_range = create_range(model->y_size);

    // scan area
  s->opt[OPT_TL_X].name = SANE_NAME_SCAN_TL_X;
  s->opt[OPT_TL_X].title = SANE_TITLE_SCAN_TL_X;
  s->opt[OPT_TL_X].desc = SANE_DESC_SCAN_TL_X;
  s->opt[OPT_TL_X].type = SANE_TYPE_FIXED;
  s->opt[OPT_TL_X].unit = SANE_UNIT_MM;
  s->opt[OPT_TL_X].constraint_type = SANE_CONSTRAINT_RANGE;

  s->opt[OPT_TL_Y].name = SANE_NAME_SCAN_TL_Y;
  s->opt[OPT_TL_Y].title = SANE_TITLE_SCAN_TL_Y;
  s->opt[OPT_TL_Y].desc = SANE_DESC_SCAN_TL_Y;
  s->opt[OPT_TL_Y].type = SANE_TYPE_FIXED;
  s->opt[OPT_TL_Y].unit = SANE_UNIT_MM;
  s->opt[OPT_TL_Y].constraint_type = SANE_CONSTRAINT_RANGE;

  s->opt[OPT_BR_X].name = SANE_NAME_SCAN_BR_X;
  s->opt[OPT_BR_X].title = SANE_TITLE_SCAN_BR_X;
  s->opt[OPT_BR_X].desc = SANE_DESC_SCAN_BR_X;
  s->opt[OPT_BR_X].type = SANE_TYPE_FIXED;
  s->opt[OPT_BR_X].unit = SANE_UNIT_MM;
  s->opt[OPT_BR_X].constraint_type = SANE_CONSTRAINT_RANGE;

  s->opt[OPT_BR_Y].name = SANE_NAME_SCAN_BR_Y;
  s->opt[OPT_BR_Y].title = SANE_TITLE_SCAN_BR_Y;
  s->opt[OPT_BR_Y].desc = SANE_DESC_SCAN_BR_Y;
  s->opt[OPT_BR_Y].type = SANE_TYPE_FIXED;
  s->opt[OPT_BR_Y].unit = SANE_UNIT_MM;
  s->opt[OPT_BR_Y].constraint_type = SANE_CONSTRAINT_RANGE;

    set_xy_range_option_values(*s);

  /* "Enhancement" group: */
  s->opt[OPT_ENHANCEMENT_GROUP].name = SANE_NAME_ENHANCEMENT;
  s->opt[OPT_ENHANCEMENT_GROUP].title = SANE_I18N ("Enhancement");
  s->opt[OPT_ENHANCEMENT_GROUP].desc = "";
  s->opt[OPT_ENHANCEMENT_GROUP].type = SANE_TYPE_GROUP;
  s->opt[OPT_ENHANCEMENT_GROUP].cap = SANE_CAP_ADVANCED;
  s->opt[OPT_ENHANCEMENT_GROUP].size = 0;
  s->opt[OPT_ENHANCEMENT_GROUP].constraint_type = SANE_CONSTRAINT_NONE;

  /* custom-gamma table */
  s->opt[OPT_CUSTOM_GAMMA].name = SANE_NAME_CUSTOM_GAMMA;
  s->opt[OPT_CUSTOM_GAMMA].title = SANE_TITLE_CUSTOM_GAMMA;
  s->opt[OPT_CUSTOM_GAMMA].desc = SANE_DESC_CUSTOM_GAMMA;
  s->opt[OPT_CUSTOM_GAMMA].type = SANE_TYPE_BOOL;
  s->opt[OPT_CUSTOM_GAMMA].cap |= SANE_CAP_ADVANCED;
  s->custom_gamma = false;

  /* grayscale gamma vector */
  s->opt[OPT_GAMMA_VECTOR].name = SANE_NAME_GAMMA_VECTOR;
  s->opt[OPT_GAMMA_VECTOR].title = SANE_TITLE_GAMMA_VECTOR;
  s->opt[OPT_GAMMA_VECTOR].desc = SANE_DESC_GAMMA_VECTOR;
  init_gamma_vector_option (s, OPT_GAMMA_VECTOR);

  /* red gamma vector */
  s->opt[OPT_GAMMA_VECTOR_R].name = SANE_NAME_GAMMA_VECTOR_R;
  s->opt[OPT_GAMMA_VECTOR_R].title = SANE_TITLE_GAMMA_VECTOR_R;
  s->opt[OPT_GAMMA_VECTOR_R].desc = SANE_DESC_GAMMA_VECTOR_R;
  init_gamma_vector_option (s, OPT_GAMMA_VECTOR_R);

  /* green gamma vector */
  s->opt[OPT_GAMMA_VECTOR_G].name = SANE_NAME_GAMMA_VECTOR_G;
  s->opt[OPT_GAMMA_VECTOR_G].title = SANE_TITLE_GAMMA_VECTOR_G;
  s->opt[OPT_GAMMA_VECTOR_G].desc = SANE_DESC_GAMMA_VECTOR_G;
  init_gamma_vector_option (s, OPT_GAMMA_VECTOR_G);

  /* blue gamma vector */
  s->opt[OPT_GAMMA_VECTOR_B].name = SANE_NAME_GAMMA_VECTOR_B;
  s->opt[OPT_GAMMA_VECTOR_B].title = SANE_TITLE_GAMMA_VECTOR_B;
  s->opt[OPT_GAMMA_VECTOR_B].desc = SANE_DESC_GAMMA_VECTOR_B;
  init_gamma_vector_option (s, OPT_GAMMA_VECTOR_B);

  /* currently, there are only gamma table options in this group,
   * so if the scanner doesn't support gamma table, disable the
   * whole group */
    if (!has_flag(model->flags, ModelFlag::CUSTOM_GAMMA)) {
      s->opt[OPT_ENHANCEMENT_GROUP].cap |= SANE_CAP_INACTIVE;
      s->opt[OPT_CUSTOM_GAMMA].cap |= SANE_CAP_INACTIVE;
      DBG(DBG_info, "%s: custom gamma disabled\n", __func__);
    }

  /* software base image enhancements, these are consuming as many
   * memory than used by the full scanned image and may fail at high
   * resolution
   */
  /* software deskew */
  s->opt[OPT_SWDESKEW].name = "swdeskew";
  s->opt[OPT_SWDESKEW].title = "Software deskew";
  s->opt[OPT_SWDESKEW].desc = "Request backend to rotate skewed pages digitally";
  s->opt[OPT_SWDESKEW].type = SANE_TYPE_BOOL;
  s->opt[OPT_SWDESKEW].cap = SANE_CAP_SOFT_SELECT | SANE_CAP_SOFT_DETECT | SANE_CAP_ADVANCED;
  s->swdeskew = false;

  /* software deskew */
  s->opt[OPT_SWDESPECK].name = "swdespeck";
  s->opt[OPT_SWDESPECK].title = "Software despeck";
  s->opt[OPT_SWDESPECK].desc = "Request backend to remove lone dots digitally";
  s->opt[OPT_SWDESPECK].type = SANE_TYPE_BOOL;
  s->opt[OPT_SWDESPECK].cap = SANE_CAP_SOFT_SELECT | SANE_CAP_SOFT_DETECT | SANE_CAP_ADVANCED;
  s->swdespeck = false;

  /* software despeckle radius */
  s->opt[OPT_DESPECK].name = "despeck";
  s->opt[OPT_DESPECK].title = "Software despeckle diameter";
  s->opt[OPT_DESPECK].desc = "Maximum diameter of lone dots to remove from scan";
  s->opt[OPT_DESPECK].type = SANE_TYPE_INT;
  s->opt[OPT_DESPECK].unit = SANE_UNIT_NONE;
  s->opt[OPT_DESPECK].constraint_type = SANE_CONSTRAINT_RANGE;
  s->opt[OPT_DESPECK].constraint.range = &swdespeck_range;
  s->opt[OPT_DESPECK].cap = SANE_CAP_SOFT_SELECT | SANE_CAP_SOFT_DETECT | SANE_CAP_ADVANCED | SANE_CAP_INACTIVE;
  s->despeck = 1;

  /* crop by software */
  s->opt[OPT_SWCROP].name = "swcrop";
  s->opt[OPT_SWCROP].title = SANE_I18N ("Software crop");
  s->opt[OPT_SWCROP].desc = SANE_I18N ("Request backend to remove border from pages digitally");
  s->opt[OPT_SWCROP].type = SANE_TYPE_BOOL;
  s->opt[OPT_SWCROP].cap = SANE_CAP_SOFT_SELECT | SANE_CAP_SOFT_DETECT | SANE_CAP_ADVANCED;
  s->opt[OPT_SWCROP].unit = SANE_UNIT_NONE;
  s->swcrop = false;

  /* Software blank page skip */
  s->opt[OPT_SWSKIP].name = "swskip";
  s->opt[OPT_SWSKIP].title = SANE_I18N ("Software blank skip percentage");
  s->opt[OPT_SWSKIP].desc = SANE_I18N("Request driver to discard pages with low numbers of dark pixels");
  s->opt[OPT_SWSKIP].type = SANE_TYPE_FIXED;
  s->opt[OPT_SWSKIP].unit = SANE_UNIT_PERCENT;
  s->opt[OPT_SWSKIP].constraint_type = SANE_CONSTRAINT_RANGE;
  s->opt[OPT_SWSKIP].constraint.range = &(percentage_range);
  s->opt[OPT_SWSKIP].cap = SANE_CAP_SOFT_SELECT | SANE_CAP_SOFT_DETECT | SANE_CAP_ADVANCED;
  s->swskip = 0;    // disable by default

  /* Software Derotate */
  s->opt[OPT_SWDEROTATE].name = "swderotate";
  s->opt[OPT_SWDEROTATE].title = SANE_I18N ("Software derotate");
  s->opt[OPT_SWDEROTATE].desc = SANE_I18N("Request driver to detect and correct 90 degree image rotation");
  s->opt[OPT_SWDEROTATE].type = SANE_TYPE_BOOL;
  s->opt[OPT_SWDEROTATE].cap = SANE_CAP_SOFT_SELECT | SANE_CAP_SOFT_DETECT | SANE_CAP_ADVANCED;
  s->opt[OPT_SWDEROTATE].unit = SANE_UNIT_NONE;
  s->swderotate = false;

  /* Software brightness */
  s->opt[OPT_BRIGHTNESS].name = SANE_NAME_BRIGHTNESS;
  s->opt[OPT_BRIGHTNESS].title = SANE_TITLE_BRIGHTNESS;
  s->opt[OPT_BRIGHTNESS].desc = SANE_DESC_BRIGHTNESS;
  s->opt[OPT_BRIGHTNESS].type = SANE_TYPE_INT;
  s->opt[OPT_BRIGHTNESS].unit = SANE_UNIT_NONE;
  s->opt[OPT_BRIGHTNESS].constraint_type = SANE_CONSTRAINT_RANGE;
  s->opt[OPT_BRIGHTNESS].constraint.range = &(enhance_range);
  s->opt[OPT_BRIGHTNESS].cap = SANE_CAP_SOFT_SELECT | SANE_CAP_SOFT_DETECT;
  s->brightness = 0;    // disable by default

  /* Sowftware contrast */
  s->opt[OPT_CONTRAST].name = SANE_NAME_CONTRAST;
  s->opt[OPT_CONTRAST].title = SANE_TITLE_CONTRAST;
  s->opt[OPT_CONTRAST].desc = SANE_DESC_CONTRAST;
  s->opt[OPT_CONTRAST].type = SANE_TYPE_INT;
  s->opt[OPT_CONTRAST].unit = SANE_UNIT_NONE;
  s->opt[OPT_CONTRAST].constraint_type = SANE_CONSTRAINT_RANGE;
  s->opt[OPT_CONTRAST].constraint.range = &(enhance_range);
  s->opt[OPT_CONTRAST].cap = SANE_CAP_SOFT_SELECT | SANE_CAP_SOFT_DETECT;
  s->contrast = 0;  // disable by default

  /* "Extras" group: */
  s->opt[OPT_EXTRAS_GROUP].name = "extras-group";
  s->opt[OPT_EXTRAS_GROUP].title = SANE_I18N ("Extras");
  s->opt[OPT_EXTRAS_GROUP].desc = "";
  s->opt[OPT_EXTRAS_GROUP].type = SANE_TYPE_GROUP;
  s->opt[OPT_EXTRAS_GROUP].cap = SANE_CAP_ADVANCED;
  s->opt[OPT_EXTRAS_GROUP].size = 0;
  s->opt[OPT_EXTRAS_GROUP].constraint_type = SANE_CONSTRAINT_NONE;

  /* BW threshold */
  s->opt[OPT_THRESHOLD].name = SANE_NAME_THRESHOLD;
  s->opt[OPT_THRESHOLD].title = SANE_TITLE_THRESHOLD;
  s->opt[OPT_THRESHOLD].desc = SANE_DESC_THRESHOLD;
  s->opt[OPT_THRESHOLD].type = SANE_TYPE_FIXED;
  s->opt[OPT_THRESHOLD].unit = SANE_UNIT_PERCENT;
  s->opt[OPT_THRESHOLD].constraint_type = SANE_CONSTRAINT_RANGE;
  s->opt[OPT_THRESHOLD].constraint.range = &percentage_range;
    s->threshold = float_to_fixed(50);

  /* BW threshold curve */
  s->opt[OPT_THRESHOLD_CURVE].name = "threshold-curve";
  s->opt[OPT_THRESHOLD_CURVE].title = SANE_I18N ("Threshold curve");
  s->opt[OPT_THRESHOLD_CURVE].desc = SANE_I18N ("Dynamic threshold curve, from light to dark, normally 50-65");
  s->opt[OPT_THRESHOLD_CURVE].type = SANE_TYPE_INT;
  s->opt[OPT_THRESHOLD_CURVE].unit = SANE_UNIT_NONE;
  s->opt[OPT_THRESHOLD_CURVE].constraint_type = SANE_CONSTRAINT_RANGE;
  s->opt[OPT_THRESHOLD_CURVE].constraint.range = &threshold_curve_range;
  s->threshold_curve = 50;

  /* disable_interpolation */
  s->opt[OPT_DISABLE_INTERPOLATION].name = "disable-interpolation";
  s->opt[OPT_DISABLE_INTERPOLATION].title =
    SANE_I18N ("Disable interpolation");
  s->opt[OPT_DISABLE_INTERPOLATION].desc =
    SANE_I18N
    ("When using high resolutions where the horizontal resolution is smaller "
     "than the vertical resolution this disables horizontal interpolation.");
  s->opt[OPT_DISABLE_INTERPOLATION].type = SANE_TYPE_BOOL;
  s->opt[OPT_DISABLE_INTERPOLATION].unit = SANE_UNIT_NONE;
  s->opt[OPT_DISABLE_INTERPOLATION].constraint_type = SANE_CONSTRAINT_NONE;
  s->disable_interpolation = false;

  /* color filter */
  s->opt[OPT_COLOR_FILTER].name = "color-filter";
  s->opt[OPT_COLOR_FILTER].title = SANE_I18N ("Color filter");
  s->opt[OPT_COLOR_FILTER].desc =
    SANE_I18N
    ("When using gray or lineart this option selects the used color.");
  s->opt[OPT_COLOR_FILTER].type = SANE_TYPE_STRING;
  s->opt[OPT_COLOR_FILTER].constraint_type = SANE_CONSTRAINT_STRING_LIST;
  /* true gray not yet supported for GL847 and GL124 scanners */
    if (!model->is_cis || model->asic_type==AsicType::GL847 || model->asic_type==AsicType::GL124) {
      s->opt[OPT_COLOR_FILTER].size = max_string_size (color_filter_list);
      s->opt[OPT_COLOR_FILTER].constraint.string_list = color_filter_list;
      s->color_filter = s->opt[OPT_COLOR_FILTER].constraint.string_list[1];
    }
  else
    {
      s->opt[OPT_COLOR_FILTER].size = max_string_size (cis_color_filter_list);
      s->opt[OPT_COLOR_FILTER].constraint.string_list = cis_color_filter_list;
      /* default to "None" ie true gray */
      s->color_filter = s->opt[OPT_COLOR_FILTER].constraint.string_list[3];
    }

    // no support for color filter for cis+gl646 scanners
    if (model->asic_type == AsicType::GL646 && model->is_cis) {
      DISABLE (OPT_COLOR_FILTER);
    }

  /* calibration store file name */
  s->opt[OPT_CALIBRATION_FILE].name = "calibration-file";
  s->opt[OPT_CALIBRATION_FILE].title = SANE_I18N ("Calibration file");
  s->opt[OPT_CALIBRATION_FILE].desc = SANE_I18N ("Specify the calibration file to use");
  s->opt[OPT_CALIBRATION_FILE].type = SANE_TYPE_STRING;
  s->opt[OPT_CALIBRATION_FILE].unit = SANE_UNIT_NONE;
  s->opt[OPT_CALIBRATION_FILE].size = PATH_MAX;
  s->opt[OPT_CALIBRATION_FILE].cap = SANE_CAP_SOFT_DETECT | SANE_CAP_SOFT_SELECT | SANE_CAP_ADVANCED;
  s->opt[OPT_CALIBRATION_FILE].constraint_type = SANE_CONSTRAINT_NONE;
  s->calibration_file.clear();
  /* disable option if ran as root */
#ifdef HAVE_GETUID
  if(geteuid()==0)
    {
      DISABLE (OPT_CALIBRATION_FILE);
    }
#endif

  /* expiration time for calibration cache entries */
  s->opt[OPT_EXPIRATION_TIME].name = "expiration-time";
  s->opt[OPT_EXPIRATION_TIME].title = SANE_I18N ("Calibration cache expiration time");
  s->opt[OPT_EXPIRATION_TIME].desc = SANE_I18N ("Time (in minutes) before a cached calibration expires. "
     "A value of 0 means cache is not used. A negative value means cache never expires.");
  s->opt[OPT_EXPIRATION_TIME].type = SANE_TYPE_INT;
  s->opt[OPT_EXPIRATION_TIME].unit = SANE_UNIT_NONE;
  s->opt[OPT_EXPIRATION_TIME].constraint_type = SANE_CONSTRAINT_RANGE;
  s->opt[OPT_EXPIRATION_TIME].constraint.range = &expiration_range;
  s->expiration_time = 60;  // 60 minutes by default

  /* Powersave time (turn lamp off) */
  s->opt[OPT_LAMP_OFF_TIME].name = "lamp-off-time";
  s->opt[OPT_LAMP_OFF_TIME].title = SANE_I18N ("Lamp off time");
  s->opt[OPT_LAMP_OFF_TIME].desc =
    SANE_I18N
    ("The lamp will be turned off after the given time (in minutes). "
     "A value of 0 means, that the lamp won't be turned off.");
  s->opt[OPT_LAMP_OFF_TIME].type = SANE_TYPE_INT;
  s->opt[OPT_LAMP_OFF_TIME].unit = SANE_UNIT_NONE;
  s->opt[OPT_LAMP_OFF_TIME].constraint_type = SANE_CONSTRAINT_RANGE;
  s->opt[OPT_LAMP_OFF_TIME].constraint.range = &time_range;
  s->lamp_off_time = 15;    // 15 minutes

  /* turn lamp off during scan */
  s->opt[OPT_LAMP_OFF].name = "lamp-off-scan";
  s->opt[OPT_LAMP_OFF].title = SANE_I18N ("Lamp off during scan");
  s->opt[OPT_LAMP_OFF].desc = SANE_I18N ("The lamp will be turned off during scan. ");
  s->opt[OPT_LAMP_OFF].type = SANE_TYPE_BOOL;
  s->opt[OPT_LAMP_OFF].unit = SANE_UNIT_NONE;
  s->opt[OPT_LAMP_OFF].constraint_type = SANE_CONSTRAINT_NONE;
  s->lamp_off = false;

  s->opt[OPT_SENSOR_GROUP].name = SANE_NAME_SENSORS;
  s->opt[OPT_SENSOR_GROUP].title = SANE_TITLE_SENSORS;
  s->opt[OPT_SENSOR_GROUP].desc = SANE_DESC_SENSORS;
  s->opt[OPT_SENSOR_GROUP].type = SANE_TYPE_GROUP;
  s->opt[OPT_SENSOR_GROUP].cap = SANE_CAP_ADVANCED;
  s->opt[OPT_SENSOR_GROUP].size = 0;
  s->opt[OPT_SENSOR_GROUP].constraint_type = SANE_CONSTRAINT_NONE;

  s->opt[OPT_SCAN_SW].name = SANE_NAME_SCAN;
  s->opt[OPT_SCAN_SW].title = SANE_TITLE_SCAN;
  s->opt[OPT_SCAN_SW].desc = SANE_DESC_SCAN;
  s->opt[OPT_SCAN_SW].type = SANE_TYPE_BOOL;
  s->opt[OPT_SCAN_SW].unit = SANE_UNIT_NONE;
  if (model->buttons & GENESYS_HAS_SCAN_SW)
    s->opt[OPT_SCAN_SW].cap =
      SANE_CAP_SOFT_DETECT | SANE_CAP_HARD_SELECT | SANE_CAP_ADVANCED;
  else
    s->opt[OPT_SCAN_SW].cap = SANE_CAP_INACTIVE;

  /* SANE_NAME_FILE is not for buttons */
  s->opt[OPT_FILE_SW].name = "file";
  s->opt[OPT_FILE_SW].title = SANE_I18N ("File button");
  s->opt[OPT_FILE_SW].desc = SANE_I18N ("File button");
  s->opt[OPT_FILE_SW].type = SANE_TYPE_BOOL;
  s->opt[OPT_FILE_SW].unit = SANE_UNIT_NONE;
  if (model->buttons & GENESYS_HAS_FILE_SW)
    s->opt[OPT_FILE_SW].cap =
      SANE_CAP_SOFT_DETECT | SANE_CAP_HARD_SELECT | SANE_CAP_ADVANCED;
  else
    s->opt[OPT_FILE_SW].cap = SANE_CAP_INACTIVE;

  s->opt[OPT_EMAIL_SW].name = SANE_NAME_EMAIL;
  s->opt[OPT_EMAIL_SW].title = SANE_TITLE_EMAIL;
  s->opt[OPT_EMAIL_SW].desc = SANE_DESC_EMAIL;
  s->opt[OPT_EMAIL_SW].type = SANE_TYPE_BOOL;
  s->opt[OPT_EMAIL_SW].unit = SANE_UNIT_NONE;
  if (model->buttons & GENESYS_HAS_EMAIL_SW)
    s->opt[OPT_EMAIL_SW].cap =
      SANE_CAP_SOFT_DETECT | SANE_CAP_HARD_SELECT | SANE_CAP_ADVANCED;
  else
    s->opt[OPT_EMAIL_SW].cap = SANE_CAP_INACTIVE;

  s->opt[OPT_COPY_SW].name = SANE_NAME_COPY;
  s->opt[OPT_COPY_SW].title = SANE_TITLE_COPY;
  s->opt[OPT_COPY_SW].desc = SANE_DESC_COPY;
  s->opt[OPT_COPY_SW].type = SANE_TYPE_BOOL;
  s->opt[OPT_COPY_SW].unit = SANE_UNIT_NONE;
  if (model->buttons & GENESYS_HAS_COPY_SW)
    s->opt[OPT_COPY_SW].cap =
      SANE_CAP_SOFT_DETECT | SANE_CAP_HARD_SELECT | SANE_CAP_ADVANCED;
  else
    s->opt[OPT_COPY_SW].cap = SANE_CAP_INACTIVE;

  s->opt[OPT_PAGE_LOADED_SW].name = SANE_NAME_PAGE_LOADED;
  s->opt[OPT_PAGE_LOADED_SW].title = SANE_TITLE_PAGE_LOADED;
  s->opt[OPT_PAGE_LOADED_SW].desc = SANE_DESC_PAGE_LOADED;
  s->opt[OPT_PAGE_LOADED_SW].type = SANE_TYPE_BOOL;
  s->opt[OPT_PAGE_LOADED_SW].unit = SANE_UNIT_NONE;
  if (model->buttons & GENESYS_HAS_PAGE_LOADED_SW)
    s->opt[OPT_PAGE_LOADED_SW].cap =
      SANE_CAP_SOFT_DETECT | SANE_CAP_HARD_SELECT | SANE_CAP_ADVANCED;
  else
    s->opt[OPT_PAGE_LOADED_SW].cap = SANE_CAP_INACTIVE;

  /* OCR button */
  s->opt[OPT_OCR_SW].name = "ocr";
  s->opt[OPT_OCR_SW].title = SANE_I18N ("OCR button");
  s->opt[OPT_OCR_SW].desc = SANE_I18N ("OCR button");
  s->opt[OPT_OCR_SW].type = SANE_TYPE_BOOL;
  s->opt[OPT_OCR_SW].unit = SANE_UNIT_NONE;
  if (model->buttons & GENESYS_HAS_OCR_SW)
    s->opt[OPT_OCR_SW].cap =
      SANE_CAP_SOFT_DETECT | SANE_CAP_HARD_SELECT | SANE_CAP_ADVANCED;
  else
    s->opt[OPT_OCR_SW].cap = SANE_CAP_INACTIVE;

  /* power button */
  s->opt[OPT_POWER_SW].name = "power";
  s->opt[OPT_POWER_SW].title = SANE_I18N ("Power button");
  s->opt[OPT_POWER_SW].desc = SANE_I18N ("Power button");
  s->opt[OPT_POWER_SW].type = SANE_TYPE_BOOL;
  s->opt[OPT_POWER_SW].unit = SANE_UNIT_NONE;
  if (model->buttons & GENESYS_HAS_POWER_SW)
    s->opt[OPT_POWER_SW].cap =
      SANE_CAP_SOFT_DETECT | SANE_CAP_HARD_SELECT | SANE_CAP_ADVANCED;
  else
    s->opt[OPT_POWER_SW].cap = SANE_CAP_INACTIVE;

  /* extra button */
  s->opt[OPT_EXTRA_SW].name = "extra";
  s->opt[OPT_EXTRA_SW].title = SANE_I18N ("Extra button");
  s->opt[OPT_EXTRA_SW].desc = SANE_I18N ("Extra button");
  s->opt[OPT_EXTRA_SW].type = SANE_TYPE_BOOL;
  s->opt[OPT_EXTRA_SW].unit = SANE_UNIT_NONE;
  if (model->buttons & GENESYS_HAS_EXTRA_SW)
    s->opt[OPT_EXTRA_SW].cap =
      SANE_CAP_SOFT_DETECT | SANE_CAP_HARD_SELECT | SANE_CAP_ADVANCED;
  else
    s->opt[OPT_EXTRA_SW].cap = SANE_CAP_INACTIVE;

  /* calibration needed */
  s->opt[OPT_NEED_CALIBRATION_SW].name = "need-calibration";
  s->opt[OPT_NEED_CALIBRATION_SW].title = SANE_I18N ("Needs calibration");
  s->opt[OPT_NEED_CALIBRATION_SW].desc = SANE_I18N ("The scanner needs calibration for the current settings");
  s->opt[OPT_NEED_CALIBRATION_SW].type = SANE_TYPE_BOOL;
  s->opt[OPT_NEED_CALIBRATION_SW].unit = SANE_UNIT_NONE;
  if (model->buttons & GENESYS_HAS_CALIBRATE)
    s->opt[OPT_NEED_CALIBRATION_SW].cap =
      SANE_CAP_SOFT_DETECT | SANE_CAP_HARD_SELECT | SANE_CAP_ADVANCED;
  else
    s->opt[OPT_NEED_CALIBRATION_SW].cap = SANE_CAP_INACTIVE;

  /* button group */
  s->opt[OPT_BUTTON_GROUP].name = "buttons";
  s->opt[OPT_BUTTON_GROUP].title = SANE_I18N ("Buttons");
  s->opt[OPT_BUTTON_GROUP].desc = "";
  s->opt[OPT_BUTTON_GROUP].type = SANE_TYPE_GROUP;
  s->opt[OPT_BUTTON_GROUP].cap = SANE_CAP_ADVANCED;
  s->opt[OPT_BUTTON_GROUP].size = 0;
  s->opt[OPT_BUTTON_GROUP].constraint_type = SANE_CONSTRAINT_NONE;

  /* calibrate button */
  s->opt[OPT_CALIBRATE].name = "calibrate";
  s->opt[OPT_CALIBRATE].title = SANE_I18N ("Calibrate");
  s->opt[OPT_CALIBRATE].desc =
    SANE_I18N ("Start calibration using special sheet");
  s->opt[OPT_CALIBRATE].type = SANE_TYPE_BUTTON;
  s->opt[OPT_CALIBRATE].unit = SANE_UNIT_NONE;
  if (model->buttons & GENESYS_HAS_CALIBRATE)
    s->opt[OPT_CALIBRATE].cap =
      SANE_CAP_SOFT_DETECT | SANE_CAP_SOFT_SELECT | SANE_CAP_ADVANCED |
      SANE_CAP_AUTOMATIC;
  else
    s->opt[OPT_CALIBRATE].cap = SANE_CAP_INACTIVE;

  /* clear calibration cache button */
  s->opt[OPT_CLEAR_CALIBRATION].name = "clear-calibration";
  s->opt[OPT_CLEAR_CALIBRATION].title = SANE_I18N ("Clear calibration");
  s->opt[OPT_CLEAR_CALIBRATION].desc = SANE_I18N ("Clear calibration cache");
  s->opt[OPT_CLEAR_CALIBRATION].type = SANE_TYPE_BUTTON;
  s->opt[OPT_CLEAR_CALIBRATION].unit = SANE_UNIT_NONE;
  s->opt[OPT_CLEAR_CALIBRATION].size = 0;
  s->opt[OPT_CLEAR_CALIBRATION].constraint_type = SANE_CONSTRAINT_NONE;
  s->opt[OPT_CLEAR_CALIBRATION].cap =
    SANE_CAP_SOFT_DETECT | SANE_CAP_SOFT_SELECT | SANE_CAP_ADVANCED;

  /* force calibration cache button */
  s->opt[OPT_FORCE_CALIBRATION].name = "force-calibration";
  s->opt[OPT_FORCE_CALIBRATION].title = SANE_I18N("Force calibration");
  s->opt[OPT_FORCE_CALIBRATION].desc = SANE_I18N("Force calibration ignoring all and any calibration caches");
  s->opt[OPT_FORCE_CALIBRATION].type = SANE_TYPE_BUTTON;
  s->opt[OPT_FORCE_CALIBRATION].unit = SANE_UNIT_NONE;
  s->opt[OPT_FORCE_CALIBRATION].size = 0;
  s->opt[OPT_FORCE_CALIBRATION].constraint_type = SANE_CONSTRAINT_NONE;
  s->opt[OPT_FORCE_CALIBRATION].cap =
    SANE_CAP_SOFT_DETECT | SANE_CAP_SOFT_SELECT | SANE_CAP_ADVANCED;

    // ignore offsets option
    s->opt[OPT_IGNORE_OFFSETS].name = "ignore-internal-offsets";
    s->opt[OPT_IGNORE_OFFSETS].title = SANE_I18N("Ignore internal offsets");
    s->opt[OPT_IGNORE_OFFSETS].desc =
        SANE_I18N("Acquires the image including the internal calibration areas of the scanner");
    s->opt[OPT_IGNORE_OFFSETS].type = SANE_TYPE_BUTTON;
    s->opt[OPT_IGNORE_OFFSETS].unit = SANE_UNIT_NONE;
    s->opt[OPT_IGNORE_OFFSETS].size = 0;
    s->opt[OPT_IGNORE_OFFSETS].constraint_type = SANE_CONSTRAINT_NONE;
    s->opt[OPT_IGNORE_OFFSETS].cap = SANE_CAP_SOFT_DETECT | SANE_CAP_SOFT_SELECT |
                                     SANE_CAP_ADVANCED;

    calc_parameters(s);
}

static bool present;

// this function is passed to C API, it must not throw
static SANE_Status
check_present (SANE_String_Const devname) noexcept
{
    DBG_HELPER_ARGS(dbg, "%s detected.", devname);
    present = true;
  return SANE_STATUS_GOOD;
}

static Genesys_Device* attach_usb_device(const char* devname,
                                         std::uint16_t vendor_id, std::uint16_t product_id)
{
    Genesys_USB_Device_Entry* found_usb_dev = nullptr;
    for (auto& usb_dev : *s_usb_devices) {
        if (usb_dev.vendor == vendor_id &&
            usb_dev.product == product_id)
        {
            found_usb_dev = &usb_dev;
            break;
        }
    }

    if (found_usb_dev == nullptr) {
        throw SaneException("vendor 0x%xd product 0x%xd is not supported by this backend",
                            vendor_id, product_id);
    }

    s_devices->emplace_back();
    Genesys_Device* dev = &s_devices->back();
    dev->file_name = devname;

    dev->model = &found_usb_dev->model;
    dev->vendorId = found_usb_dev->vendor;
    dev->productId = found_usb_dev->product;
    dev->usb_mode = 0; // i.e. unset
    dev->already_initialized = false;
    return dev;
}

static Genesys_Device* attach_device_by_name(SANE_String_Const devname, bool may_wait)
{
    DBG_HELPER_ARGS(dbg, " devname: %s, may_wait = %d", devname, may_wait);

    if (!devname) {
        throw SaneException("devname must not be nullptr");
    }

    for (auto& dev : *s_devices) {
        if (dev.file_name == devname) {
            DBG(DBG_info, "%s: device `%s' was already in device list\n", __func__, devname);
            return &dev;
        }
    }

  DBG(DBG_info, "%s: trying to open device `%s'\n", __func__, devname);

    UsbDevice usb_dev;

    usb_dev.open(devname);
    DBG(DBG_info, "%s: device `%s' successfully opened\n", __func__, devname);

    int vendor, product;
    usb_dev.get_vendor_product(vendor, product);
    usb_dev.close();

  /* KV-SS080 is an auxiliary device which requires a master device to be here */
  if(vendor == 0x04da && product == 0x100f)
    {
        present = false;
      sanei_usb_find_devices (vendor, 0x1006, check_present);
      sanei_usb_find_devices (vendor, 0x1007, check_present);
      sanei_usb_find_devices (vendor, 0x1010, check_present);
        if (present == false) {
            throw SaneException("master device not present");
        }
    }

    Genesys_Device* dev = attach_usb_device(devname, vendor, product);

    DBG(DBG_info, "%s: found %s flatbed scanner %s at %s\n", __func__, dev->model->vendor,
        dev->model->model, dev->file_name.c_str());

    return dev;
}

// this function is passed to C API and must not throw
static SANE_Status attach_one_device(SANE_String_Const devname) noexcept
{
    DBG_HELPER(dbg);
    return wrap_exceptions_to_status_code(__func__, [=]()
    {
        attach_device_by_name(devname, false);
    });
}

/* configuration framework functions */

// this function is passed to C API, it must not throw
static SANE_Status
config_attach_genesys(SANEI_Config __sane_unused__ *config, const char *devname) noexcept
{
  /* the devname has been processed and is ready to be used
   * directly. Since the backend is an USB only one, we can
   * call sanei_usb_attach_matching_devices straight */
  sanei_usb_attach_matching_devices (devname, attach_one_device);

  return SANE_STATUS_GOOD;
}

/* probes for scanner to attach to the backend */
static void probe_genesys_devices()
{
    DBG_HELPER(dbg);
    if (is_testing_mode()) {
        attach_usb_device(get_testing_device_name().c_str(),
                          get_testing_vendor_id(), get_testing_product_id());
        return;
    }

  SANEI_Config config;

    // set configuration options structure : no option for this backend
    config.descriptors = nullptr;
    config.values = nullptr;
  config.count = 0;

    TIE(sanei_configure_attach(GENESYS_CONFIG_FILE, &config, config_attach_genesys));

    DBG(DBG_info, "%s: %zu devices currently attached\n", __func__, s_devices->size());
}

/**
 * This should be changed if one of the substructures of
   Genesys_Calibration_Cache change, but it must be changed if there are
   changes that don't change size -- at least for now, as we store most
   of Genesys_Calibration_Cache as is.
*/
static const char* CALIBRATION_IDENT = "sane_genesys";
static const int CALIBRATION_VERSION = 27;

bool read_calibration(std::istream& str, Genesys_Device::Calibration& calibration,
                      const std::string& path)
{
    DBG_HELPER(dbg);

    std::string ident;
    serialize(str, ident);

    if (ident != CALIBRATION_IDENT) {
        DBG(DBG_info, "%s: Incorrect calibration file '%s' header\n", __func__, path.c_str());
        return false;
    }

    size_t version;
    serialize(str, version);

    if (version != CALIBRATION_VERSION) {
        DBG(DBG_info, "%s: Incorrect calibration file '%s' version\n", __func__, path.c_str());
        return false;
    }

    calibration.clear();
    serialize(str, calibration);
    return true;
}

/**
 * reads previously cached calibration data
 * from file defined in dev->calib_file
 */
static bool sanei_genesys_read_calibration(Genesys_Device::Calibration& calibration,
                                           const std::string& path)
{
    DBG_HELPER(dbg);

    std::ifstream str;
    str.open(path);
    if (!str.is_open()) {
        DBG(DBG_info, "%s: Cannot open %s\n", __func__, path.c_str());
        return false;
    }

    return read_calibration(str, calibration, path);
}

void write_calibration(std::ostream& str, Genesys_Device::Calibration& calibration)
{
    std::string ident = CALIBRATION_IDENT;
    serialize(str, ident);
    size_t version = CALIBRATION_VERSION;
    serialize(str, version);
    serialize_newline(str);
    serialize(str, calibration);
}

static void write_calibration(Genesys_Device::Calibration& calibration, const std::string& path)
{
    DBG_HELPER(dbg);

    std::ofstream str;
    str.open(path);
    if (!str.is_open()) {
        throw SaneException("Cannot open calibration for writing");
    }
    write_calibration(str, calibration);
}

/** @brief buffer scanned picture
 * In order to allow digital processing, we must be able to put all the
 * scanned picture in a buffer.
 */
static void genesys_buffer_image(Genesys_Scanner *s)
{
    DBG_HELPER(dbg);
  size_t maximum;     /**> maximum bytes size of the scan */
  size_t len;	      /**> length of scanned data read */
  size_t total;	      /**> total of butes read */
  size_t size;	      /**> size of image buffer */
  size_t read_size;   /**> size of reads */
  int lines;	      /** number of lines of the scan */
  Genesys_Device *dev = s->dev;

  /* compute maximum number of lines for the scan */
  if (s->params.lines > 0)
    {
      lines = s->params.lines;
    }
  else
    {
        lines = static_cast<int>((dev->model->y_size * dev->settings.yres) / MM_PER_INCH);
    }
  DBG(DBG_info, "%s: buffering %d lines of %d bytes\n", __func__, lines,
      s->params.bytes_per_line);

  /* maximum bytes to read */
  maximum = s->params.bytes_per_line * lines;
    if (s->dev->settings.scan_mode == ScanColorMode::LINEART) {
      maximum *= 8;
    }

  /* initial size of the read buffer */
  size =
    ((2048 * 2048) / s->params.bytes_per_line) * s->params.bytes_per_line;

  /* read size */
  read_size = size / 2;

  dev->img_buffer.resize(size);

  /* loop reading data until we reach maximum or EOF */
    total = 0;
    while (total < maximum) {
      len = size - maximum;
      if (len > read_size)
	{
	  len = read_size;
	}

        try {
            genesys_read_ordered_data(dev, dev->img_buffer.data() + total, &len);
        } catch (const SaneException& e) {
            if (e.status() == SANE_STATUS_EOF) {
                // ideally we shouldn't end up here, but because computations are duplicated and
                // slightly different everywhere in the genesys backend, we have no other choice
                break;
            }
            throw;
        }
      total += len;

        // do we need to enlarge read buffer ?
        if (total + read_size > size) {
            size += read_size;
            dev->img_buffer.resize(size);
        }
    }

  /* since digital processing is going to take place,
   * issue head parking command so that the head move while
   * computing so we can save time
   */
    if (!dev->model->is_sheetfed && !dev->parking) {
        dev->cmd_set->move_back_home(dev, has_flag(dev->model->flags, ModelFlag::MUST_WAIT));
        dev->parking = !has_flag(s->dev->model->flags, ModelFlag::MUST_WAIT);
    }

  /* in case of dynamic lineart, we have buffered gray data which
   * must be converted to lineart first */
    if (s->dev->settings.scan_mode == ScanColorMode::LINEART) {
      total/=8;
      std::vector<uint8_t> lineart(total);

      genesys_gray_lineart (dev,
                            dev->img_buffer.data(),
                            lineart.data(),
                            dev->settings.pixels,
                            (total*8)/dev->settings.pixels,
                            dev->settings.threshold);
      dev->img_buffer = lineart;
    }

  /* update counters */
  dev->total_bytes_to_read = total;
  dev->total_bytes_read = 0;

  /* update params */
  s->params.lines = total / s->params.bytes_per_line;
  if (DBG_LEVEL >= DBG_io2)
    {
      sanei_genesys_write_pnm_file("gl_unprocessed.pnm", dev->img_buffer.data(), s->params.depth,
                                   s->params.format==SANE_FRAME_RGB ? 3 : 1,
                                   s->params.pixels_per_line, s->params.lines);
    }
}

/* -------------------------- SANE API functions ------------------------- */

void sane_init_impl(SANE_Int * version_code, SANE_Auth_Callback authorize)
{
  DBG_INIT ();
    DBG_HELPER_ARGS(dbg, "authorize %s null", authorize ? "!=" : "==");
    DBG(DBG_init, "SANE Genesys backend from %s\n", PACKAGE_STRING);

    if (!is_testing_mode()) {
#ifdef HAVE_LIBUSB
        DBG(DBG_init, "SANE Genesys backend built with libusb-1.0\n");
#endif
#ifdef HAVE_LIBUSB_LEGACY
        DBG(DBG_init, "SANE Genesys backend built with libusb\n");
#endif
    }

    if (version_code) {
        *version_code = SANE_VERSION_CODE(SANE_CURRENT_MAJOR, SANE_CURRENT_MINOR, 0);
    }

    if (!is_testing_mode()) {
        sanei_usb_init();
    }

  /* init sanei_magic */
  sanei_magic_init();

  s_scanners.init();
  s_devices.init();
  s_sane_devices.init();
    s_sane_devices_data.init();
  s_sane_devices_ptrs.init();
  genesys_init_sensor_tables();
  genesys_init_frontend_tables();
    genesys_init_gpo_tables();
    genesys_init_motor_tables();
    genesys_init_usb_device_tables();


  DBG(DBG_info, "%s: %s endian machine\n", __func__,
#ifdef WORDS_BIGENDIAN
       "big"
#else
       "little"
#endif
    );

    // cold-plug case :detection of allready connected scanners
    probe_genesys_devices();
}


SANE_GENESYS_API_LINKAGE
SANE_Status sane_init(SANE_Int * version_code, SANE_Auth_Callback authorize)
{
    return wrap_exceptions_to_status_code(__func__, [=]()
    {
        sane_init_impl(version_code, authorize);
    });
}

void
sane_exit_impl(void)
{
    DBG_HELPER(dbg);

    if (!is_testing_mode()) {
        sanei_usb_exit();
    }

  run_functions_at_backend_exit();
}

SANE_GENESYS_API_LINKAGE
void sane_exit()
{
    catch_all_exceptions(__func__, [](){ sane_exit_impl(); });
}

void sane_get_devices_impl(const SANE_Device *** device_list, SANE_Bool local_only)
{
    DBG_HELPER_ARGS(dbg, "local_only = %s", local_only ? "true" : "false");

    if (!is_testing_mode()) {
        // hot-plug case : detection of newly connected scanners */
        sanei_usb_scan_devices();
    }
    probe_genesys_devices();

    s_sane_devices->clear();
    s_sane_devices_data->clear();
    s_sane_devices_ptrs->clear();
    s_sane_devices->reserve(s_devices->size());
    s_sane_devices_data->reserve(s_devices->size());
    s_sane_devices_ptrs->reserve(s_devices->size() + 1);

    for (auto dev_it = s_devices->begin(); dev_it != s_devices->end();) {

        if (is_testing_mode()) {
            present = true;
        } else {
            present = false;
            sanei_usb_find_devices(dev_it->vendorId, dev_it->productId, check_present);
        }

        if (present) {
            s_sane_devices->emplace_back();
            s_sane_devices_data->emplace_back();
            auto& sane_device = s_sane_devices->back();
            auto& sane_device_data = s_sane_devices_data->back();
            sane_device_data.name = dev_it->file_name;
            sane_device.name = sane_device_data.name.c_str();
            sane_device.vendor = dev_it->model->vendor;
            sane_device.model = dev_it->model->model;
            sane_device.type = "flatbed scanner";
            s_sane_devices_ptrs->push_back(&sane_device);
            dev_it++;
        } else {
            dev_it = s_devices->erase(dev_it);
        }
    }
    s_sane_devices_ptrs->push_back(nullptr);

    *const_cast<SANE_Device***>(device_list) = s_sane_devices_ptrs->data();
}

SANE_GENESYS_API_LINKAGE
SANE_Status sane_get_devices(const SANE_Device *** device_list, SANE_Bool local_only)
{
    return wrap_exceptions_to_status_code(__func__, [=]()
    {
        sane_get_devices_impl(device_list, local_only);
    });
}

static void sane_open_impl(SANE_String_Const devicename, SANE_Handle * handle)
{
    DBG_HELPER_ARGS(dbg, "devicename = %s", devicename);
    Genesys_Device* dev = nullptr;

  /* devicename="" or devicename="genesys" are default values that use
   * first available device
   */
    if (devicename[0] && strcmp ("genesys", devicename) != 0) {
      /* search for the given devicename in the device list */
        for (auto& d : *s_devices) {
            if (d.file_name == devicename) {
                dev = &d;
                break;
            }
        }

        if (dev) {
            DBG(DBG_info, "%s: found `%s' in devlist\n", __func__, dev->model->name);
        } else if (is_testing_mode()) {
            DBG(DBG_info, "%s: couldn't find `%s' in devlist, not attaching", __func__, devicename);
        } else {
            DBG(DBG_info, "%s: couldn't find `%s' in devlist, trying attach\n", __func__,
                devicename);
            dbg.status("attach_device_by_name");
            dev = attach_device_by_name(devicename, true);
            dbg.clear();
        }
    } else {
        // empty devicename or "genesys" -> use first device
        if (!s_devices->empty()) {
            dev = &s_devices->front();
            DBG(DBG_info, "%s: empty devicename, trying `%s'\n", __func__, dev->file_name.c_str());
        }
    }

    if (!dev) {
        throw SaneException("could not find the device to open: %s", devicename);
    }

    if (has_flag(dev->model->flags, ModelFlag::UNTESTED)) {
      DBG(DBG_error0, "WARNING: Your scanner is not fully supported or at least \n");
      DBG(DBG_error0, "         had only limited testing. Please be careful and \n");
      DBG(DBG_error0, "         report any failure/success to \n");
      DBG(DBG_error0, "         sane-devel@alioth-lists.debian.net. Please provide as many\n");
      DBG(DBG_error0, "         details as possible, e.g. the exact name of your\n");
      DBG(DBG_error0, "         scanner and what does (not) work.\n");
    }

    dbg.vstatus("open device '%s'", dev->file_name.c_str());

    if (is_testing_mode()) {
        auto interface = std::unique_ptr<TestScannerInterface>{new TestScannerInterface{dev}};
        interface->set_checkpoint_callback(get_testing_checkpoint_callback());
        dev->interface = std::move(interface);
    } else {
        dev->interface = std::unique_ptr<ScannerInterfaceUsb>{new ScannerInterfaceUsb{dev}};
    }
    dev->interface->get_usb_device().open(dev->file_name.c_str());
    dbg.clear();

  s_scanners->push_back(Genesys_Scanner());
  auto* s = &s_scanners->back();

  s->dev = dev;
    s->scanning = false;
    s->dev->parking = false;
    s->dev->read_active = false;
  s->dev->force_calibration = 0;
  s->dev->line_count = 0;

  *handle = s;

    if (!dev->already_initialized) {
        sanei_genesys_init_structs (dev);
    }

    init_options(s);

    sanei_genesys_init_cmd_set(s->dev);

    // FIXME: we create sensor tables for the sensor, this should happen when we know which sensor
    // we will select
    dev->cmd_set->init(dev);

    // some hardware capabilities are detected through sensors
    s->dev->cmd_set->update_hardware_sensors (s);

  /* here is the place to fetch a stored calibration cache */
  if (s->dev->force_calibration == 0)
    {
        auto path = calibration_filename(s->dev);
        s->calibration_file = path;
        s->dev->calib_file = path;
      DBG(DBG_info, "%s: Calibration filename set to:\n", __func__);
      DBG(DBG_info, "%s: >%s<\n", __func__, s->dev->calib_file.c_str());

        catch_all_exceptions(__func__, [&]()
        {
            sanei_genesys_read_calibration(s->dev->calibration_cache, s->dev->calib_file);
        });
    }
}

SANE_GENESYS_API_LINKAGE
SANE_Status sane_open(SANE_String_Const devicename, SANE_Handle* handle)
{
    return wrap_exceptions_to_status_code(__func__, [=]()
    {
        sane_open_impl(devicename, handle);
    });
}

void
sane_close_impl(SANE_Handle handle)
{
    DBG_HELPER(dbg);

  /* remove handle from list of open handles: */
  auto it = s_scanners->end();
  for (auto it2 = s_scanners->begin(); it2 != s_scanners->end(); it2++)
    {
      if (&*it2 == handle) {
          it = it2;
          break;
        }
    }
  if (it == s_scanners->end())
    {
      DBG(DBG_error, "%s: invalid handle %p\n", __func__, handle);
      return;			/* oops, not a handle we know about */
    }

  Genesys_Scanner* s = &*it;

  /* eject document for sheetfed scanners */
    if (s->dev->model->is_sheetfed) {
        catch_all_exceptions(__func__, [&](){ s->dev->cmd_set->eject_document(s->dev); });
    }
  else
    {
      /* in case scanner is parking, wait for the head
       * to reach home position */
        if (s->dev->parking) {
            sanei_genesys_wait_for_home(s->dev);
        }
    }

    // enable power saving before leaving
    s->dev->cmd_set->save_power(s->dev, true);

    // here is the place to store calibration cache
    if (s->dev->force_calibration == 0 && !is_testing_mode()) {
        catch_all_exceptions(__func__, [&](){ write_calibration(s->dev->calibration_cache,
                                                                s->dev->calib_file); });
    }

    s->dev->already_initialized = false;

  s->dev->clear();

    // LAMP OFF : same register across all the ASICs */
    s->dev->interface->write_register(0x03, 0x00);

    catch_all_exceptions(__func__, [&](){ s->dev->interface->get_usb_device().clear_halt(); });

    // we need this to avoid these ASIC getting stuck in bulk writes
    catch_all_exceptions(__func__, [&](){ s->dev->interface->get_usb_device().reset(); });

    // not freeing s->dev because it's in the dev list
    catch_all_exceptions(__func__, [&](){ s->dev->interface->get_usb_device().close(); });

  s_scanners->erase(it);
}

SANE_GENESYS_API_LINKAGE
void sane_close(SANE_Handle handle)
{
    catch_all_exceptions(__func__, [=]()
    {
        sane_close_impl(handle);
    });
}

const SANE_Option_Descriptor *
sane_get_option_descriptor_impl(SANE_Handle handle, SANE_Int option)
{
    DBG_HELPER(dbg);
    Genesys_Scanner* s = reinterpret_cast<Genesys_Scanner*>(handle);

    if (static_cast<unsigned>(option) >= NUM_OPTIONS) {
        return nullptr;
    }

  DBG(DBG_io2, "%s: option = %s (%d)\n", __func__, s->opt[option].name, option);
  return s->opt + option;
}


SANE_GENESYS_API_LINKAGE
const SANE_Option_Descriptor* sane_get_option_descriptor(SANE_Handle handle, SANE_Int option)
{
    const SANE_Option_Descriptor* ret = nullptr;
    catch_all_exceptions(__func__, [&]()
    {
        ret = sane_get_option_descriptor_impl(handle, option);
    });
    return ret;
}

static void print_option(DebugMessageHelper& dbg, const Genesys_Scanner& s, int option, void* val)
{
    switch (s.opt[option].type) {
        case SANE_TYPE_INT: {
            dbg.vlog(DBG_proc, "value: %d", *reinterpret_cast<SANE_Word*>(val));
            return;
        }
        case SANE_TYPE_BOOL: {
            dbg.vlog(DBG_proc, "value: %s", *reinterpret_cast<SANE_Bool*>(val) ? "true" : "false");
            return;
        }
        case SANE_TYPE_FIXED: {
            dbg.vlog(DBG_proc, "value: %f", fixed_to_float(*reinterpret_cast<SANE_Word*>(val)));
            return;
        }
        case SANE_TYPE_STRING: {
            dbg.vlog(DBG_proc, "value: %s", reinterpret_cast<char*>(val));
            return;
        }
        default: break;
    }
    dbg.log(DBG_proc, "value: (non-printable)");
}

static void get_option_value(Genesys_Scanner* s, int option, void* val)
{
    DBG_HELPER_ARGS(dbg, "option: %s (%d)", s->opt[option].name, option);
  unsigned int i;
    SANE_Word* table = nullptr;
  std::vector<uint16_t> gamma_table;
  unsigned option_size = 0;

    const Genesys_Sensor* sensor = nullptr;
    if (sanei_genesys_has_sensor(s->dev, s->dev->settings.xres, s->dev->settings.get_channels(),
                                 s->dev->settings.scan_method))
    {
        sensor = &sanei_genesys_find_sensor(s->dev, s->dev->settings.xres,
                                            s->dev->settings.get_channels(),
                                            s->dev->settings.scan_method);
    }

  switch (option)
    {
      /* geometry */
    case OPT_TL_X:
        *reinterpret_cast<SANE_Word*>(val) = s->pos_top_left_x;
        break;
    case OPT_TL_Y:
        *reinterpret_cast<SANE_Word*>(val) = s->pos_top_left_y;
        break;
    case OPT_BR_X:
        *reinterpret_cast<SANE_Word*>(val) = s->pos_bottom_right_x;
        break;
    case OPT_BR_Y:
        *reinterpret_cast<SANE_Word*>(val) = s->pos_bottom_right_y;
        break;
      /* word options: */
    case OPT_NUM_OPTS:
        *reinterpret_cast<SANE_Word*>(val) = NUM_OPTIONS;
        break;
    case OPT_RESOLUTION:
        *reinterpret_cast<SANE_Word*>(val) = s->resolution;
        break;
    case OPT_BIT_DEPTH:
        *reinterpret_cast<SANE_Word*>(val) = s->bit_depth;
        break;
    case OPT_PREVIEW:
        *reinterpret_cast<SANE_Word*>(val) = s->preview;
        break;
    case OPT_THRESHOLD:
        *reinterpret_cast<SANE_Word*>(val) = s->threshold;
        break;
    case OPT_THRESHOLD_CURVE:
        *reinterpret_cast<SANE_Word*>(val) = s->threshold_curve;
        break;
    case OPT_DISABLE_INTERPOLATION:
        *reinterpret_cast<SANE_Word*>(val) = s->disable_interpolation;
        break;
    case OPT_LAMP_OFF:
        *reinterpret_cast<SANE_Word*>(val) = s->lamp_off;
        break;
    case OPT_LAMP_OFF_TIME:
        *reinterpret_cast<SANE_Word*>(val) = s->lamp_off_time;
        break;
    case OPT_SWDESKEW:
        *reinterpret_cast<SANE_Word*>(val) = s->swdeskew;
        break;
    case OPT_SWCROP:
        *reinterpret_cast<SANE_Word*>(val) = s->swcrop;
        break;
    case OPT_SWDESPECK:
        *reinterpret_cast<SANE_Word*>(val) = s->swdespeck;
        break;
    case OPT_SWDEROTATE:
        *reinterpret_cast<SANE_Word*>(val) = s->swderotate;
        break;
    case OPT_SWSKIP:
        *reinterpret_cast<SANE_Word*>(val) = s->swskip;
        break;
    case OPT_DESPECK:
        *reinterpret_cast<SANE_Word*>(val) = s->despeck;
        break;
    case OPT_CONTRAST:
        *reinterpret_cast<SANE_Word*>(val) = s->contrast;
        break;
    case OPT_BRIGHTNESS:
        *reinterpret_cast<SANE_Word*>(val) = s->brightness;
        break;
    case OPT_EXPIRATION_TIME:
        *reinterpret_cast<SANE_Word*>(val) = s->expiration_time;
        break;
    case OPT_CUSTOM_GAMMA:
        *reinterpret_cast<SANE_Word*>(val) = s->custom_gamma;
        break;

      /* string options: */
    case OPT_MODE:
        std::strcpy(reinterpret_cast<char*>(val), s->mode.c_str());
        break;
    case OPT_COLOR_FILTER:
        std::strcpy(reinterpret_cast<char*>(val), s->color_filter.c_str());
        break;
    case OPT_CALIBRATION_FILE:
        std::strcpy(reinterpret_cast<char*>(val), s->calibration_file.c_str());
        break;
    case OPT_SOURCE:
        std::strcpy(reinterpret_cast<char*>(val), scan_method_to_option_string(s->scan_method));
        break;

      /* word array options */
    case OPT_GAMMA_VECTOR:
        if (!sensor)
            throw SaneException("Unsupported scanner mode selected");

        table = reinterpret_cast<SANE_Word*>(val);
        if (s->color_filter == "Red") {
            gamma_table = get_gamma_table(s->dev, *sensor, GENESYS_RED);
        } else if (s->color_filter == "Blue") {
            gamma_table = get_gamma_table(s->dev, *sensor, GENESYS_BLUE);
        } else {
            gamma_table = get_gamma_table(s->dev, *sensor, GENESYS_GREEN);
        }
        option_size = s->opt[option].size / sizeof (SANE_Word);
        if (gamma_table.size() != option_size) {
            throw std::runtime_error("The size of the gamma tables does not match");
        }
        for (i = 0; i < option_size; i++) {
            table[i] = gamma_table[i];
        }
        break;
    case OPT_GAMMA_VECTOR_R:
        if (!sensor)
            throw SaneException("Unsupported scanner mode selected");

        table = reinterpret_cast<SANE_Word*>(val);
        gamma_table = get_gamma_table(s->dev, *sensor, GENESYS_RED);
        option_size = s->opt[option].size / sizeof (SANE_Word);
        if (gamma_table.size() != option_size) {
            throw std::runtime_error("The size of the gamma tables does not match");
        }
        for (i = 0; i < option_size; i++) {
            table[i] = gamma_table[i];
	}
      break;
    case OPT_GAMMA_VECTOR_G:
        if (!sensor)
            throw SaneException("Unsupported scanner mode selected");

        table = reinterpret_cast<SANE_Word*>(val);
        gamma_table = get_gamma_table(s->dev, *sensor, GENESYS_GREEN);
        option_size = s->opt[option].size / sizeof (SANE_Word);
        if (gamma_table.size() != option_size) {
            throw std::runtime_error("The size of the gamma tables does not match");
        }
        for (i = 0; i < option_size; i++) {
            table[i] = gamma_table[i];
        }
      break;
    case OPT_GAMMA_VECTOR_B:
        if (!sensor)
            throw SaneException("Unsupported scanner mode selected");

        table = reinterpret_cast<SANE_Word*>(val);
        gamma_table = get_gamma_table(s->dev, *sensor, GENESYS_BLUE);
        option_size = s->opt[option].size / sizeof (SANE_Word);
        if (gamma_table.size() != option_size) {
            throw std::runtime_error("The size of the gamma tables does not match");
        }
        for (i = 0; i < option_size; i++) {
            table[i] = gamma_table[i];
        }
      break;
      /* sensors */
    case OPT_SCAN_SW:
    case OPT_FILE_SW:
    case OPT_EMAIL_SW:
    case OPT_COPY_SW:
    case OPT_PAGE_LOADED_SW:
    case OPT_OCR_SW:
    case OPT_POWER_SW:
    case OPT_EXTRA_SW:
        s->dev->cmd_set->update_hardware_sensors(s);
        *reinterpret_cast<SANE_Bool*>(val) = s->buttons[genesys_option_to_button(option)].read();
        break;

        case OPT_NEED_CALIBRATION_SW: {
            if (!sensor) {
                throw SaneException("Unsupported scanner mode selected");
            }

            // scanner needs calibration for current mode unless a matching calibration cache is
            // found

            bool result = true;

            auto session = s->dev->cmd_set->calculate_scan_session(s->dev, *sensor,
                                                                   s->dev->settings);

            for (auto& cache : s->dev->calibration_cache) {
                if (sanei_genesys_is_compatible_calibration(s->dev, session, &cache, false)) {
                    *reinterpret_cast<SANE_Bool*>(val) = SANE_FALSE;
                }
            }
            *reinterpret_cast<SANE_Bool*>(val) = result;
            break;
        }
    default:
      DBG(DBG_warn, "%s: can't get unknown option %d\n", __func__, option);
    }
    print_option(dbg, *s, option, val);
}

/** @brief set calibration file value
 * Set calibration file value. Load new cache values from file if it exists,
 * else creates the file*/
static void set_calibration_value(Genesys_Scanner* s, const char* val)
{
    DBG_HELPER(dbg);

    std::string new_calib_path = val;
    Genesys_Device::Calibration new_calibration;

    bool is_calib_success = false;
    catch_all_exceptions(__func__, [&]()
    {
        is_calib_success = sanei_genesys_read_calibration(new_calibration, new_calib_path);
    });

    if (!is_calib_success) {
        return;
    }

    s->dev->calibration_cache = std::move(new_calibration);
    s->dev->calib_file = new_calib_path;
    s->calibration_file = new_calib_path;
    DBG(DBG_info, "%s: Calibration filename set to '%s':\n", __func__, new_calib_path.c_str());
}

/* sets an option , called by sane_control_option */
static void set_option_value(Genesys_Scanner* s, int option, void *val, SANE_Int* myinfo)
{
    DBG_HELPER_ARGS(dbg, "option: %s (%d)", s->opt[option].name, option);
    print_option(dbg, *s, option, val);

  SANE_Word *table;
  unsigned int i;
  unsigned option_size = 0;

  switch (option)
    {
    case OPT_TL_X:
        s->pos_top_left_x = *reinterpret_cast<SANE_Word*>(val);
        calc_parameters(s);
        *myinfo |= SANE_INFO_RELOAD_PARAMS;
        break;
    case OPT_TL_Y:
        s->pos_top_left_y = *reinterpret_cast<SANE_Word*>(val);
        calc_parameters(s);
        *myinfo |= SANE_INFO_RELOAD_PARAMS;
        break;
    case OPT_BR_X:
        s->pos_bottom_right_x = *reinterpret_cast<SANE_Word*>(val);
        calc_parameters(s);
        *myinfo |= SANE_INFO_RELOAD_PARAMS;
        break;
    case OPT_BR_Y:
        s->pos_bottom_right_y = *reinterpret_cast<SANE_Word*>(val);
        calc_parameters(s);
        *myinfo |= SANE_INFO_RELOAD_PARAMS;
        break;
    case OPT_RESOLUTION:
        s->resolution = *reinterpret_cast<SANE_Word*>(val);
        calc_parameters(s);
        *myinfo |= SANE_INFO_RELOAD_PARAMS;
        break;
    case OPT_THRESHOLD:
        s->threshold = *reinterpret_cast<SANE_Word*>(val);
        calc_parameters(s);
        *myinfo |= SANE_INFO_RELOAD_PARAMS;
        break;
    case OPT_THRESHOLD_CURVE:
        s->threshold_curve = *reinterpret_cast<SANE_Word*>(val);
        calc_parameters(s);
        *myinfo |= SANE_INFO_RELOAD_PARAMS;
        break;
    case OPT_SWCROP:
        s->swcrop = *reinterpret_cast<SANE_Word*>(val);
        calc_parameters(s);
        *myinfo |= SANE_INFO_RELOAD_PARAMS;
        break;
    case OPT_SWDESKEW:
        s->swdeskew = *reinterpret_cast<SANE_Word*>(val);
        calc_parameters(s);
        *myinfo |= SANE_INFO_RELOAD_PARAMS;
        break;
    case OPT_DESPECK:
        s->despeck = *reinterpret_cast<SANE_Word*>(val);
        calc_parameters(s);
        *myinfo |= SANE_INFO_RELOAD_PARAMS;
        break;
    case OPT_SWDEROTATE:
        s->swderotate = *reinterpret_cast<SANE_Word*>(val);
        calc_parameters(s);
        *myinfo |= SANE_INFO_RELOAD_PARAMS;
        break;
    case OPT_SWSKIP:
        s->swskip = *reinterpret_cast<SANE_Word*>(val);
        calc_parameters(s);
        *myinfo |= SANE_INFO_RELOAD_PARAMS;
        break;
    case OPT_DISABLE_INTERPOLATION:
        s->disable_interpolation = *reinterpret_cast<SANE_Word*>(val);
        calc_parameters(s);
        *myinfo |= SANE_INFO_RELOAD_PARAMS;
        break;
    case OPT_LAMP_OFF:
        s->lamp_off = *reinterpret_cast<SANE_Word*>(val);
        calc_parameters(s);
        *myinfo |= SANE_INFO_RELOAD_PARAMS;
        break;
    case OPT_PREVIEW:
        s->preview = *reinterpret_cast<SANE_Word*>(val);
        calc_parameters(s);
        *myinfo |= SANE_INFO_RELOAD_PARAMS;
        break;
    case OPT_BRIGHTNESS:
        s->brightness = *reinterpret_cast<SANE_Word*>(val);
        calc_parameters(s);
        *myinfo |= SANE_INFO_RELOAD_PARAMS;
        break;
    case OPT_CONTRAST:
        s->contrast = *reinterpret_cast<SANE_Word*>(val);
        calc_parameters(s);
        *myinfo |= SANE_INFO_RELOAD_PARAMS;
        break;
    case OPT_SWDESPECK:
        s->swdespeck = *reinterpret_cast<SANE_Word*>(val);
        if (s->swdespeck) {
            ENABLE(OPT_DESPECK);
        } else {
            DISABLE(OPT_DESPECK);
        }
        calc_parameters(s);
      *myinfo |= SANE_INFO_RELOAD_PARAMS | SANE_INFO_RELOAD_OPTIONS;
      break;
    /* software enhancement functions only apply to 8 or 1 bits data */
    case OPT_BIT_DEPTH:
        s->bit_depth = *reinterpret_cast<SANE_Word*>(val);
        if(s->bit_depth>8)
        {
          DISABLE(OPT_SWDESKEW);
          DISABLE(OPT_SWDESPECK);
          DISABLE(OPT_SWCROP);
          DISABLE(OPT_DESPECK);
          DISABLE(OPT_SWDEROTATE);
          DISABLE(OPT_SWSKIP);
          DISABLE(OPT_CONTRAST);
          DISABLE(OPT_BRIGHTNESS);
        }
      else
        {
          ENABLE(OPT_SWDESKEW);
          ENABLE(OPT_SWDESPECK);
          ENABLE(OPT_SWCROP);
          ENABLE(OPT_DESPECK);
          ENABLE(OPT_SWDEROTATE);
          ENABLE(OPT_SWSKIP);
          ENABLE(OPT_CONTRAST);
          ENABLE(OPT_BRIGHTNESS);
        }
        calc_parameters(s);
      *myinfo |= SANE_INFO_RELOAD_PARAMS | SANE_INFO_RELOAD_OPTIONS;
      break;
    case OPT_SOURCE: {
        auto scan_method = option_string_to_scan_method(reinterpret_cast<const char*>(val));
        if (s->scan_method != scan_method) {
            s->scan_method = scan_method;

            set_xy_range_option_values(*s);
            set_resolution_option_values(*s, false);

            *myinfo |= SANE_INFO_RELOAD_PARAMS | SANE_INFO_RELOAD_OPTIONS;
        }
        break;
    }
    case OPT_MODE:
      s->mode = reinterpret_cast<const char*>(val);

      if (s->mode == SANE_VALUE_SCAN_MODE_LINEART)
	{
	  ENABLE (OPT_THRESHOLD);
	  ENABLE (OPT_THRESHOLD_CURVE);
	  DISABLE (OPT_BIT_DEPTH);
                if (s->dev->model->asic_type != AsicType::GL646 || !s->dev->model->is_cis) {
                    ENABLE(OPT_COLOR_FILTER);
                }
	}
      else
	{
	  DISABLE (OPT_THRESHOLD);
	  DISABLE (OPT_THRESHOLD_CURVE);
          if (s->mode == SANE_VALUE_SCAN_MODE_GRAY)
	    {
                    if (s->dev->model->asic_type != AsicType::GL646 || !s->dev->model->is_cis) {
                        ENABLE(OPT_COLOR_FILTER);
                    }
	      create_bpp_list (s, s->dev->model->bpp_gray_values);
                    s->bit_depth = s->dev->model->bpp_gray_values[0];
	    }
	  else
	    {
	      DISABLE (OPT_COLOR_FILTER);
	      create_bpp_list (s, s->dev->model->bpp_color_values);
                    s->bit_depth = s->dev->model->bpp_color_values[0];
	    }
	}
        calc_parameters(s);

      /* if custom gamma, toggle gamma table options according to the mode */
      if (s->custom_gamma)
	{
          if (s->mode == SANE_VALUE_SCAN_MODE_COLOR)
	    {
	      DISABLE (OPT_GAMMA_VECTOR);
	      ENABLE (OPT_GAMMA_VECTOR_R);
	      ENABLE (OPT_GAMMA_VECTOR_G);
	      ENABLE (OPT_GAMMA_VECTOR_B);
	    }
	  else
	    {
	      ENABLE (OPT_GAMMA_VECTOR);
	      DISABLE (OPT_GAMMA_VECTOR_R);
	      DISABLE (OPT_GAMMA_VECTOR_G);
	      DISABLE (OPT_GAMMA_VECTOR_B);
	    }
	}

      *myinfo |= SANE_INFO_RELOAD_PARAMS | SANE_INFO_RELOAD_OPTIONS;
      break;
    case OPT_COLOR_FILTER:
      s->color_filter = reinterpret_cast<const char*>(val);
        calc_parameters(s);
      break;
    case OPT_CALIBRATION_FILE:
            if (s->dev->force_calibration == 0) {
                set_calibration_value(s, reinterpret_cast<const char*>(val));
            }
            break;
    case OPT_LAMP_OFF_TIME:
        if (*reinterpret_cast<SANE_Word*>(val) != s->lamp_off_time) {
            s->lamp_off_time = *reinterpret_cast<SANE_Word*>(val);
                s->dev->cmd_set->set_powersaving(s->dev, s->lamp_off_time);
        }
        break;
    case OPT_EXPIRATION_TIME:
        if (*reinterpret_cast<SANE_Word*>(val) != s->expiration_time) {
            s->expiration_time = *reinterpret_cast<SANE_Word*>(val);
        }
        break;

    case OPT_CUSTOM_GAMMA:
      *myinfo |= SANE_INFO_RELOAD_PARAMS | SANE_INFO_RELOAD_OPTIONS;
        s->custom_gamma = *reinterpret_cast<SANE_Bool*>(val);

        if (s->custom_gamma) {
          if (s->mode == SANE_VALUE_SCAN_MODE_COLOR)
	    {
	      DISABLE (OPT_GAMMA_VECTOR);
	      ENABLE (OPT_GAMMA_VECTOR_R);
	      ENABLE (OPT_GAMMA_VECTOR_G);
	      ENABLE (OPT_GAMMA_VECTOR_B);
	    }
	  else
	    {
	      ENABLE (OPT_GAMMA_VECTOR);
	      DISABLE (OPT_GAMMA_VECTOR_R);
	      DISABLE (OPT_GAMMA_VECTOR_G);
	      DISABLE (OPT_GAMMA_VECTOR_B);
	    }
	}
      else
	{
	  DISABLE (OPT_GAMMA_VECTOR);
	  DISABLE (OPT_GAMMA_VECTOR_R);
	  DISABLE (OPT_GAMMA_VECTOR_G);
	  DISABLE (OPT_GAMMA_VECTOR_B);
            for (auto& table : s->dev->gamma_override_tables) {
                table.clear();
            }
	}
      break;

    case OPT_GAMMA_VECTOR:
        table = reinterpret_cast<SANE_Word*>(val);
        option_size = s->opt[option].size / sizeof (SANE_Word);

        s->dev->gamma_override_tables[GENESYS_RED].resize(option_size);
        s->dev->gamma_override_tables[GENESYS_GREEN].resize(option_size);
        s->dev->gamma_override_tables[GENESYS_BLUE].resize(option_size);
        for (i = 0; i < option_size; i++) {
            s->dev->gamma_override_tables[GENESYS_RED][i] = table[i];
            s->dev->gamma_override_tables[GENESYS_GREEN][i] = table[i];
            s->dev->gamma_override_tables[GENESYS_BLUE][i] = table[i];
        }
      break;
    case OPT_GAMMA_VECTOR_R:
        table = reinterpret_cast<SANE_Word*>(val);
        option_size = s->opt[option].size / sizeof (SANE_Word);
        s->dev->gamma_override_tables[GENESYS_RED].resize(option_size);
        for (i = 0; i < option_size; i++) {
            s->dev->gamma_override_tables[GENESYS_RED][i] = table[i];
        }
      break;
    case OPT_GAMMA_VECTOR_G:
        table = reinterpret_cast<SANE_Word*>(val);
        option_size = s->opt[option].size / sizeof (SANE_Word);
        s->dev->gamma_override_tables[GENESYS_GREEN].resize(option_size);
        for (i = 0; i < option_size; i++) {
            s->dev->gamma_override_tables[GENESYS_GREEN][i] = table[i];
        }
      break;
    case OPT_GAMMA_VECTOR_B:
        table = reinterpret_cast<SANE_Word*>(val);
        option_size = s->opt[option].size / sizeof (SANE_Word);
        s->dev->gamma_override_tables[GENESYS_BLUE].resize(option_size);
        for (i = 0; i < option_size; i++) {
            s->dev->gamma_override_tables[GENESYS_BLUE][i] = table[i];
        }
      break;
        case OPT_CALIBRATE: {
            auto& sensor = sanei_genesys_find_sensor_for_write(s->dev, s->dev->settings.xres,
                                                               s->dev->settings.get_channels(),
                                                               s->dev->settings.scan_method);
            catch_all_exceptions(__func__, [&]()
            {
                s->dev->cmd_set->save_power(s->dev, false);
                genesys_scanner_calibration(s->dev, sensor);
            });
            catch_all_exceptions(__func__, [&]()
            {
                s->dev->cmd_set->save_power(s->dev, true);
            });
            *myinfo |= SANE_INFO_RELOAD_PARAMS | SANE_INFO_RELOAD_OPTIONS;
            break;
        }
    case OPT_CLEAR_CALIBRATION:
      s->dev->calibration_cache.clear();

      /* remove file */
      unlink(s->dev->calib_file.c_str());
      /* signals that sensors will have to be read again */
      *myinfo |= SANE_INFO_RELOAD_PARAMS | SANE_INFO_RELOAD_OPTIONS;
      break;
    case OPT_FORCE_CALIBRATION:
      s->dev->force_calibration = 1;
      s->dev->calibration_cache.clear();
      s->dev->calib_file.clear();

      /* signals that sensors will have to be read again */
      *myinfo |= SANE_INFO_RELOAD_PARAMS | SANE_INFO_RELOAD_OPTIONS;
      break;

        case OPT_IGNORE_OFFSETS: {
            s->dev->ignore_offsets = true;
            break;
        }
    default:
      DBG(DBG_warn, "%s: can't set unknown option %d\n", __func__, option);
    }
}


/* sets and gets scanner option values */
void sane_control_option_impl(SANE_Handle handle, SANE_Int option,
                              SANE_Action action, void *val, SANE_Int * info)
{
    Genesys_Scanner* s = reinterpret_cast<Genesys_Scanner*>(handle);
    auto action_str = (action == SANE_ACTION_GET_VALUE) ? "get" :
                      (action == SANE_ACTION_SET_VALUE) ? "set" :
                      (action == SANE_ACTION_SET_AUTO) ? "set_auto" : "unknown";
    DBG_HELPER_ARGS(dbg, "action = %s, option = %s (%d)", action_str,
                    s->opt[option].name, option);

  SANE_Word cap;
  SANE_Int myinfo = 0;

    if (info) {
        *info = 0;
    }

    if (s->scanning) {
        throw SaneException(SANE_STATUS_DEVICE_BUSY,
                            "don't call this function while scanning (option = %s (%d))",
                            s->opt[option].name, option);
    }
    if (option >= NUM_OPTIONS || option < 0) {
        throw SaneException("option %d >= NUM_OPTIONS || option < 0", option);
    }

  cap = s->opt[option].cap;

    if (!SANE_OPTION_IS_ACTIVE (cap)) {
        throw SaneException("option %d is inactive", option);
    }

    switch (action) {
        case SANE_ACTION_GET_VALUE:
            get_option_value(s, option, val);
            break;

        case SANE_ACTION_SET_VALUE:
            if (!SANE_OPTION_IS_SETTABLE (cap)) {
                throw SaneException("option %d is not settable", option);
            }

            TIE(sanei_constrain_value(s->opt + option, val, &myinfo));

            set_option_value(s, option, val, &myinfo);
            break;

        case SANE_ACTION_SET_AUTO:
            throw SaneException("SANE_ACTION_SET_AUTO unsupported since no option "
                                "has SANE_CAP_AUTOMATIC");
        default:
            throw SaneException("unknown action %d for option %d", action, option);
    }

  if (info)
    *info = myinfo;
}

SANE_GENESYS_API_LINKAGE
SANE_Status sane_control_option(SANE_Handle handle, SANE_Int option,
                                           SANE_Action action, void *val, SANE_Int * info)
{
    return wrap_exceptions_to_status_code(__func__, [=]()
    {
        sane_control_option_impl(handle, option, action, val, info);
    });
}

void sane_get_parameters_impl(SANE_Handle handle, SANE_Parameters* params)
{
    DBG_HELPER(dbg);
    Genesys_Scanner* s = reinterpret_cast<Genesys_Scanner*>(handle);

  /* don't recompute parameters once data reading is active, ie during scan */
    if (!s->dev->read_active) {
        calc_parameters(s);
    }
  if (params)
    {
      *params = s->params;

      /* in the case of a sheetfed scanner, when full height is specified
       * we override the computed line number with -1 to signal that we
       * don't know the real document height.
       * We don't do that doing buffering image for digital processing
       */
        if (s->dev->model->is_sheetfed && !s->dev->buffer_image &&
            s->pos_bottom_right_y == s->opt[OPT_BR_Y].constraint.range->max)
        {
	  params->lines = -1;
	}
    }
    debug_dump(DBG_proc, *params);
}

SANE_GENESYS_API_LINKAGE
SANE_Status sane_get_parameters(SANE_Handle handle, SANE_Parameters* params)
{
    return wrap_exceptions_to_status_code(__func__, [=]()
    {
        sane_get_parameters_impl(handle, params);
    });
}

void sane_start_impl(SANE_Handle handle)
{
    DBG_HELPER(dbg);
    Genesys_Scanner* s = reinterpret_cast<Genesys_Scanner*>(handle);

    if (s->pos_top_left_x >= s->pos_bottom_right_x) {
        throw SaneException("top left x >= bottom right x");
    }
    if (s->pos_top_left_y >= s->pos_bottom_right_y) {
        throw SaneException("top left y >= bottom right y");
    }

  /* First make sure we have a current parameter set.  Some of the
     parameters will be overwritten below, but that's OK.  */

    calc_parameters(s);
    genesys_start_scan(s->dev, s->lamp_off);

    s->scanning = true;

  /* allocate intermediate buffer when doing dynamic lineart */
    if (s->dev->settings.scan_mode == ScanColorMode::LINEART) {
        s->dev->binarize_buffer.clear();
        s->dev->binarize_buffer.alloc(s->dev->settings.pixels);
        s->dev->local_buffer.clear();
        s->dev->local_buffer.alloc(s->dev->binarize_buffer.size() * 8);
    }

  /* if one of the software enhancement option is selected,
   * we do the scan internally, process picture then put it an internal
   * buffer. Since cropping may change scan parameters, we recompute them
   * at the end */
  if (s->dev->buffer_image)
    {
        genesys_buffer_image(s);

      /* check if we need to skip this page, sheetfed scanners
       * can go to next doc while flatbed ones can't */
        if (s->swskip > 0 && IS_ACTIVE(OPT_SWSKIP)) {
            auto status = sanei_magic_isBlank(&s->params,
                                              s->dev->img_buffer.data(),
                                              fixed_to_float(s->swskip));

            if (status == SANE_STATUS_NO_DOCS && s->dev->model->is_sheetfed) {
                DBG(DBG_info, "%s: blank page, recurse\n", __func__);
                sane_start(handle);
                return;
            }

            if (status != SANE_STATUS_GOOD) {
                throw SaneException(status);
            }
        }

        if (s->swdeskew) {
            const auto& sensor = sanei_genesys_find_sensor(s->dev, s->dev->settings.xres,
                                                           s->dev->settings.get_channels(),
                                                           s->dev->settings.scan_method);
            catch_all_exceptions(__func__, [&](){ genesys_deskew(s, sensor); });
        }

        if (s->swdespeck) {
            catch_all_exceptions(__func__, [&](){ genesys_despeck(s); });
        }

        if(s->swcrop) {
            catch_all_exceptions(__func__, [&](){ genesys_crop(s); });
        }

        if(s->swderotate) {
            catch_all_exceptions(__func__, [&](){ genesys_derotate(s); });
        }
    }
}

SANE_GENESYS_API_LINKAGE
SANE_Status sane_start(SANE_Handle handle)
{
    return wrap_exceptions_to_status_code(__func__, [=]()
    {
        sane_start_impl(handle);
    });
}

void sane_read_impl(SANE_Handle handle, SANE_Byte * buf, SANE_Int max_len, SANE_Int* len)
{
    DBG_HELPER(dbg);
    Genesys_Scanner* s = reinterpret_cast<Genesys_Scanner*>(handle);
  Genesys_Device *dev;
  size_t local_len;

    if (!s) {
        throw SaneException("handle is nullptr");
    }

  dev=s->dev;
    if (!dev) {
        throw SaneException("dev is nullptr");
    }

    if (!buf) {
        throw SaneException("buf is nullptr");
    }

    if (!len) {
        throw SaneException("len is nullptr");
    }

  *len = 0;

    if (!s->scanning) {
        throw SaneException(SANE_STATUS_CANCELLED,
                            "scan was cancelled, is over or has not been initiated yet");
    }

  DBG(DBG_proc, "%s: start, %d maximum bytes required\n", __func__, max_len);
    DBG(DBG_io2, "%s: bytes_to_read=%zu, total_bytes_read=%zu\n", __func__,
        dev->total_bytes_to_read, dev->total_bytes_read);

  if(dev->total_bytes_read>=dev->total_bytes_to_read)
    {
      DBG(DBG_proc, "%s: nothing more to scan: EOF\n", __func__);

      /* issue park command immediatly in case scanner can handle it
       * so we save time */
        if (!dev->model->is_sheetfed && !has_flag(dev->model->flags, ModelFlag::MUST_WAIT) &&
            !dev->parking)
        {
            dev->cmd_set->move_back_home(dev, false);
            dev->parking = true;
        }
        throw SaneException(SANE_STATUS_EOF);
    }

  local_len = max_len;

  /* in case of image processing, all data has been stored in
   * buffer_image. So read data from it if it exists, else from scanner */
  if(!dev->buffer_image)
    {
      /* dynamic lineart is another kind of digital processing that needs
       * another layer of buffering on top of genesys_read_ordered_data */
        if (dev->settings.scan_mode == ScanColorMode::LINEART) {
          /* if buffer is empty, fill it with genesys_read_ordered_data */
          if(dev->binarize_buffer.avail() == 0)
            {
              /* store gray data */
              local_len=dev->local_buffer.size();
              dev->local_buffer.reset();
                genesys_read_ordered_data(dev, dev->local_buffer.get_write_pos(local_len),
                                          &local_len);
              dev->local_buffer.produce(local_len);

                dev->binarize_buffer.reset();
                if (!is_testing_mode()) {
                    genesys_gray_lineart(dev, dev->local_buffer.get_read_pos(),
                                         dev->binarize_buffer.get_write_pos(local_len / 8),
                                         dev->settings.pixels,
                                         local_len / dev->settings.pixels,
                                         dev->settings.threshold);
                }
                dev->binarize_buffer.produce(local_len / 8);
            }

          /* return data from lineart buffer if any, up to the available amount */
          local_len = max_len;
          if (static_cast<std::size_t>(max_len) > dev->binarize_buffer.avail())
            {
              local_len=dev->binarize_buffer.avail();
            }
          if(local_len)
            {
              memcpy(buf, dev->binarize_buffer.get_read_pos(), local_len);
              dev->binarize_buffer.consume(local_len);
            }
        }
      else
        {
            // most usual case, direct read of data from scanner */
            genesys_read_ordered_data(dev, buf, &local_len);
        }
    }
  else /* read data from buffer */
    {
      if(dev->total_bytes_read+local_len>dev->total_bytes_to_read)
        {
          local_len=dev->total_bytes_to_read-dev->total_bytes_read;
        }
      memcpy(buf, dev->img_buffer.data() + dev->total_bytes_read, local_len);
      dev->total_bytes_read+=local_len;
    }

  *len = local_len;
    if (local_len > static_cast<std::size_t>(max_len)) {
      fprintf (stderr, "[genesys] sane_read: returning incorrect length!!\n");
    }
  DBG(DBG_proc, "%s: %d bytes returned\n", __func__, *len);
}

SANE_GENESYS_API_LINKAGE
SANE_Status sane_read(SANE_Handle handle, SANE_Byte * buf, SANE_Int max_len, SANE_Int* len)
{
    return wrap_exceptions_to_status_code(__func__, [=]()
    {
        sane_read_impl(handle, buf, max_len, len);
    });
}

void sane_cancel_impl(SANE_Handle handle)
{
    DBG_HELPER(dbg);
    Genesys_Scanner* s = reinterpret_cast<Genesys_Scanner*>(handle);

    s->scanning = false;
    s->dev->read_active = false;
  s->dev->img_buffer.clear();

  /* no need to end scan if we are parking the head */
    if (!s->dev->parking) {
        s->dev->cmd_set->end_scan(s->dev, &s->dev->reg, true);
    }

  /* park head if flatbed scanner */
    if (!s->dev->model->is_sheetfed) {
        if (!s->dev->parking) {
            s->dev->cmd_set->move_back_home (s->dev, has_flag(s->dev->model->flags,
                                                              ModelFlag::MUST_WAIT));

            s->dev->parking = !has_flag(s->dev->model->flags, ModelFlag::MUST_WAIT);
        }
    }
  else
    {				/* in case of sheetfed scanners, we have to eject the document if still present */
        s->dev->cmd_set->eject_document(s->dev);
    }

  /* enable power saving mode unless we are parking .... */
    if (!s->dev->parking) {
        s->dev->cmd_set->save_power(s->dev, true);
    }

  return;
}

SANE_GENESYS_API_LINKAGE
void sane_cancel(SANE_Handle handle)
{
    catch_all_exceptions(__func__, [=]() { sane_cancel_impl(handle); });
}

void sane_set_io_mode_impl(SANE_Handle handle, SANE_Bool non_blocking)
{
    DBG_HELPER_ARGS(dbg, "handle = %p, non_blocking = %s", handle,
                    non_blocking == SANE_TRUE ? "true" : "false");
    Genesys_Scanner* s = reinterpret_cast<Genesys_Scanner*>(handle);

    if (!s->scanning) {
        throw SaneException("not scanning");
    }
    if (non_blocking) {
        throw SaneException(SANE_STATUS_UNSUPPORTED);
    }
}

SANE_GENESYS_API_LINKAGE
SANE_Status sane_set_io_mode(SANE_Handle handle, SANE_Bool non_blocking)
{
    return wrap_exceptions_to_status_code(__func__, [=]()
    {
        sane_set_io_mode_impl(handle, non_blocking);
    });
}

void sane_get_select_fd_impl(SANE_Handle handle, SANE_Int* fd)
{
    DBG_HELPER_ARGS(dbg, "handle = %p, fd = %p", handle, reinterpret_cast<void*>(fd));
    Genesys_Scanner* s = reinterpret_cast<Genesys_Scanner*>(handle);

    if (!s->scanning) {
        throw SaneException("not scanning");
    }
    throw SaneException(SANE_STATUS_UNSUPPORTED);
}

SANE_GENESYS_API_LINKAGE
SANE_Status sane_get_select_fd(SANE_Handle handle, SANE_Int* fd)
{
    return wrap_exceptions_to_status_code(__func__, [=]()
    {
        sane_get_select_fd_impl(handle, fd);
    });
}

GenesysButtonName genesys_option_to_button(int option)
{
    switch (option) {
    case OPT_SCAN_SW: return BUTTON_SCAN_SW;
    case OPT_FILE_SW: return BUTTON_FILE_SW;
    case OPT_EMAIL_SW: return BUTTON_EMAIL_SW;
    case OPT_COPY_SW: return BUTTON_COPY_SW;
    case OPT_PAGE_LOADED_SW: return BUTTON_PAGE_LOADED_SW;
    case OPT_OCR_SW: return BUTTON_OCR_SW;
    case OPT_POWER_SW: return BUTTON_POWER_SW;
    case OPT_EXTRA_SW: return BUTTON_EXTRA_SW;
    default: throw std::runtime_error("Unknown option to convert to button index");
    }
}

} // namespace genesys
