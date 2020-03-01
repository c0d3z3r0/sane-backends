/* sane - Scanner Access Now Easy.

   Copyright (C) 2019 Povilas Kanapickas <povilas@radix.lt>

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

#define DEBUG_DECLARE_ONLY

#include "low.h"

namespace genesys {

inline unsigned default_get_register_hwdpi(const Genesys_Sensor& sensor, unsigned xres)
{
    if (sensor.logical_dpihw_override)
        return sensor.logical_dpihw_override;

    // can't be below 600 dpi
    if (xres <= 600) {
        return 600;
    }
    if (xres <= static_cast<unsigned>(sensor.optical_res) / 4) {
        return sensor.optical_res / 4;
    }
    if (xres <= static_cast<unsigned>(sensor.optical_res) / 2) {
        return sensor.optical_res / 2;
    }
    return sensor.optical_res;
}

inline unsigned get_sensor_optical_with_ccd_divisor(const Genesys_Sensor& sensor, unsigned xres)
{
    unsigned hwres = sensor.optical_res / sensor.get_ccd_size_divisor_for_dpi(xres);

    if (xres <= hwres / 4) {
        return hwres / 4;
    }
    if (xres <= hwres / 2) {
        return hwres / 2;
    }
    return hwres;
}

inline unsigned default_get_ccd_size_divisor_for_dpi(const Genesys_Sensor& sensor, unsigned xres)
{
    if (sensor.ccd_size_divisor >= 4 && xres * 4 <= static_cast<unsigned>(sensor.optical_res)) {
        return 4;
    }
    if (sensor.ccd_size_divisor >= 2 && xres * 2 <= static_cast<unsigned>(sensor.optical_res)) {
        return 2;
    }
    return 1;
}

inline unsigned get_ccd_size_divisor_exact(const Genesys_Sensor& sensor, unsigned xres)
{
    (void) xres;
    return sensor.ccd_size_divisor;
}

inline unsigned get_ccd_size_divisor_gl124(const Genesys_Sensor& sensor, unsigned xres)
{
    // we have 2 domains for ccd: xres below or above half ccd max dpi
    if (xres <= 300 && sensor.ccd_size_divisor > 1) {
        return 2;
    }
    return 1;
}

inline unsigned default_get_hwdpi_divisor_for_dpi(const Genesys_Sensor& sensor, unsigned xres)
{
    return sensor.optical_res / default_get_register_hwdpi(sensor, xres);
}

StaticInit<std::vector<Genesys_Sensor>> s_sensors;

void genesys_init_sensor_tables()
{
    s_sensors.init();

    Genesys_Sensor sensor;

    sensor = Genesys_Sensor();
    sensor.sensor_id = SensorId::CCD_UMAX;
    sensor.optical_res = 1200;
    sensor.black_pixels = 48;
    sensor.dummy_pixel = 64;
    sensor.ccd_start_xoffset = 0;
    sensor.fau_gain_white_ref = 210;
    sensor.gain_white_ref = 230;
    sensor.exposure = { 0x0000, 0x0000, 0x0000 };
    sensor.custom_regs = {
        { 0x08, 0x01 },
        { 0x09, 0x03 },
        { 0x0a, 0x05 },
        { 0x0b, 0x07 },
        { 0x16, 0x33 },
        { 0x17, 0x05 },
        { 0x18, 0x31 },
        { 0x19, 0x2a },
        { 0x1a, 0x00 },
        { 0x1b, 0x00 },
        { 0x1c, 0x00 },
        { 0x1d, 0x02 },
        { 0x52, 0x13 },
        { 0x53, 0x17 },
        { 0x54, 0x03 },
        { 0x55, 0x07 },
        { 0x56, 0x0b },
        { 0x57, 0x0f },
        { 0x58, 0x23 },
        { 0x59, 0x00 },
        { 0x5a, 0xc1 },
        { 0x5b, 0x00 },
        { 0x5c, 0x00 },
        { 0x5d, 0x00 },
        { 0x5e, 0x00 },
    };
    sensor.gamma = { 1.0f, 1.0f, 1.0f };
    sensor.get_register_hwdpi_fun = default_get_register_hwdpi;
    sensor.get_hwdpi_divisor_fun = default_get_hwdpi_divisor_for_dpi;
    sensor.get_ccd_size_divisor_fun = default_get_ccd_size_divisor_for_dpi;
    s_sensors->push_back(sensor);


    sensor = Genesys_Sensor();
    sensor.sensor_id = SensorId::CCD_ST12;
    sensor.optical_res = 600;
    sensor.black_pixels = 48;
    sensor.dummy_pixel = 85;
    sensor.ccd_start_xoffset = 152;
    sensor.fau_gain_white_ref = 210;
    sensor.gain_white_ref = 230;
    sensor.exposure = { 0x0000, 0x0000, 0x0000 };
    sensor.custom_regs = {
        { 0x08, 0x02 },
        { 0x09, 0x00 },
        { 0x0a, 0x06 },
        { 0x0b, 0x04 },
        { 0x16, 0x2b },
        { 0x17, 0x08 },
        { 0x18, 0x20 },
        { 0x19, 0x2a },
        { 0x1a, 0x00 },
        { 0x1b, 0x00 },
        { 0x1c, 0x0c },
        { 0x1d, 0x03 },
        { 0x52, 0x0f },
        { 0x53, 0x13 },
        { 0x54, 0x17 },
        { 0x55, 0x03 },
        { 0x56, 0x07 },
        { 0x57, 0x0b },
        { 0x58, 0x83 },
        { 0x59, 0x00 },
        { 0x5a, 0xc1 },
        { 0x5b, 0x00 },
        { 0x5c, 0x00 },
        { 0x5d, 0x00 },
        { 0x5e, 0x00 },
    };
    sensor.gamma = { 1.0f, 1.0f, 1.0f };
    sensor.get_register_hwdpi_fun = default_get_register_hwdpi;
    sensor.get_hwdpi_divisor_fun = default_get_hwdpi_divisor_for_dpi;
    sensor.get_ccd_size_divisor_fun = default_get_ccd_size_divisor_for_dpi;
    s_sensors->push_back(sensor);


    sensor = Genesys_Sensor();
    sensor.sensor_id = SensorId::CCD_ST24;
    sensor.optical_res = 1200;
    sensor.black_pixels = 48;
    sensor.dummy_pixel = 64;
    sensor.ccd_start_xoffset = 0;
    sensor.fau_gain_white_ref = 210;
    sensor.gain_white_ref = 230;
    sensor.exposure = { 0x0000, 0x0000, 0x0000 };
    sensor.custom_regs = {
        { 0x08, 0x0e },
        { 0x09, 0x0c },
        { 0x0a, 0x00 },
        { 0x0b, 0x0c },
        { 0x16, 0x33 },
        { 0x17, 0x08 },
        { 0x18, 0x31 },
        { 0x19, 0x2a },
        { 0x1a, 0x00 },
        { 0x1b, 0x00 },
        { 0x1c, 0x00 },
        { 0x1d, 0x02 },
        { 0x52, 0x17 },
        { 0x53, 0x03 },
        { 0x54, 0x07 },
        { 0x55, 0x0b },
        { 0x56, 0x0f },
        { 0x57, 0x13 },
        { 0x58, 0x03 },
        { 0x59, 0x00 },
        { 0x5a, 0xc1 },
        { 0x5b, 0x00 },
        { 0x5c, 0x00 },
        { 0x5d, 0x00 },
        { 0x5e, 0x00 },
    };
    sensor.gamma = { 1.0f, 1.0f, 1.0f };
    sensor.get_register_hwdpi_fun = default_get_register_hwdpi;
    sensor.get_hwdpi_divisor_fun = default_get_hwdpi_divisor_for_dpi;
    sensor.get_ccd_size_divisor_fun = default_get_ccd_size_divisor_for_dpi;
    s_sensors->push_back(sensor);


    sensor = Genesys_Sensor();
    sensor.sensor_id = SensorId::CCD_5345;
    sensor.optical_res = 1200;
    sensor.ccd_size_divisor = 2;
    sensor.black_pixels = 48;
    sensor.dummy_pixel = 16;
    sensor.ccd_start_xoffset = 0;
    sensor.fau_gain_white_ref = 190;
    sensor.gain_white_ref = 190;
    sensor.exposure = { 0x0000, 0x0000, 0x0000 };
    sensor.stagger_config = StaggerConfig{ 1200, 4 }; // FIXME: may be incorrect
    sensor.custom_base_regs = {
        { 0x08, 0x0d },
        { 0x09, 0x0f },
        { 0x0a, 0x11 },
        { 0x0b, 0x13 },
        { 0x16, 0x0b },
        { 0x17, 0x0a },
        { 0x18, 0x30 },
        { 0x19, 0x2a },
        { 0x1a, 0x00 },
        { 0x1b, 0x00 },
        { 0x1c, 0x00 },
        { 0x1d, 0x03 },
        { 0x52, 0x0f },
        { 0x53, 0x13 },
        { 0x54, 0x17 },
        { 0x55, 0x03 },
        { 0x56, 0x07 },
        { 0x57, 0x0b },
        { 0x58, 0x23 },
        { 0x59, 0x00 },
        { 0x5a, 0xc1 },
        { 0x5b, 0x00 },
        { 0x5c, 0x00 },
        { 0x5d, 0x00 },
        { 0x5e, 0x00 },
    };
    sensor.gamma = { 2.38f, 2.35f, 2.34f };
    sensor.get_register_hwdpi_fun = default_get_register_hwdpi;
    sensor.get_hwdpi_divisor_fun = default_get_hwdpi_divisor_for_dpi;
    sensor.get_ccd_size_divisor_fun = get_ccd_size_divisor_exact;

    {
        struct CustomSensorSettings {
            ResolutionFilter resolutions;
            unsigned exposure_lperiod;
            unsigned ccd_size_divisor;
            GenesysRegisterSettingSet custom_regs;
        };

        CustomSensorSettings custom_settings[] = {
            { { 50 }, 12000, 2, {
                    { 0x08, 0x00 },
                    { 0x09, 0x05 },
                    { 0x0a, 0x06 },
                    { 0x0b, 0x08 },
                    { 0x16, 0x0b },
                    { 0x17, 0x0a },
                    { 0x18, 0x28 },
                    { 0x19, 0x2a },
                    { 0x1a, 0x00 },
                    { 0x1b, 0x00 },
                    { 0x1c, 0x00 },
                    { 0x1d, 0x03 },
                    { 0x52, 0x0f },
                    { 0x53, 0x13 },
                    { 0x54, 0x17 },
                    { 0x55, 0x03 },
                    { 0x56, 0x07 },
                    { 0x57, 0x0b },
                    { 0x58, 0x83 },
                    { 0x59, 0x00 },
                    { 0x5a, 0xc1 },
                    { 0x5b, 0x00 },
                    { 0x5c, 0x00 },
                    { 0x5d, 0x00 },
                    { 0x5e, 0x00 }
                }
            },
            { { 75 }, 11000, 2, {
                    { 0x08, 0x00 },
                    { 0x09, 0x05 },
                    { 0x0a, 0x06 },
                    { 0x0b, 0x08 },
                    { 0x16, 0x0b },
                    { 0x17, 0x0a },
                    { 0x18, 0x28 },
                    { 0x19, 0x2a },
                    { 0x1a, 0x00 },
                    { 0x1b, 0x00 },
                    { 0x1c, 0x00 },
                    { 0x1d, 0x03 },
                    { 0x52, 0x0f },
                    { 0x53, 0x13 },
                    { 0x54, 0x17 },
                    { 0x55, 0x03 },
                    { 0x56, 0x07 },
                    { 0x57, 0x0b },
                    { 0x58, 0x83 },
                    { 0x59, 0x00 },
                    { 0x5a, 0xc1 },
                    { 0x5b, 0x00 },
                    { 0x5c, 0x00 },
                    { 0x5d, 0x00 },
                    { 0x5e, 0x00 }
                }
            },
            { { 100 }, 11000, 2, {
                    { 0x08, 0x00 },
                    { 0x09, 0x05 },
                    { 0x0a, 0x06 },
                    { 0x0b, 0x08 },
                    { 0x16, 0x0b },
                    { 0x17, 0x0a },
                    { 0x18, 0x28 },
                    { 0x19, 0x2a },
                    { 0x1a, 0x00 },
                    { 0x1b, 0x00 },
                    { 0x1c, 0x00 },
                    { 0x1d, 0x03 },
                    { 0x52, 0x0f },
                    { 0x53, 0x13 },
                    { 0x54, 0x17 },
                    { 0x55, 0x03 },
                    { 0x56, 0x07 },
                    { 0x57, 0x0b },
                    { 0x58, 0x83 },
                    { 0x59, 0x00 },
                    { 0x5a, 0xc1 },
                    { 0x5b, 0x00 },
                    { 0x5c, 0x00 },
                    { 0x5d, 0x00 },
                    { 0x5e, 0x00 }
                }
            },
            { { 150 }, 11000, 2, {
                    { 0x08, 0x00 },
                    { 0x09, 0x05 },
                    { 0x0a, 0x06 },
                    { 0x0b, 0x08 },
                    { 0x16, 0x0b },
                    { 0x17, 0x0a },
                    { 0x18, 0x28 },
                    { 0x19, 0x2a },
                    { 0x1a, 0x00 },
                    { 0x1b, 0x00 },
                    { 0x1c, 0x00 },
                    { 0x1d, 0x03 },
                    { 0x52, 0x0f },
                    { 0x53, 0x13 },
                    { 0x54, 0x17 },
                    { 0x55, 0x03 },
                    { 0x56, 0x07 },
                    { 0x57, 0x0b },
                    { 0x58, 0x83 },
                    { 0x59, 0x00 },
                    { 0x5a, 0xc1 },
                    { 0x5b, 0x00 },
                    { 0x5c, 0x00 },
                    { 0x5d, 0x00 },
                    { 0x5e, 0x00 }
                }
            },
            { { 200 }, 11000, 2, {
                    { 0x08, 0x00 },
                    { 0x09, 0x05 },
                    { 0x0a, 0x06 },
                    { 0x0b, 0x08 },
                    { 0x16, 0x0b },
                    { 0x17, 0x0a },
                    { 0x18, 0x28 },
                    { 0x19, 0x2a },
                    { 0x1a, 0x00 },
                    { 0x1b, 0x00 },
                    { 0x1c, 0x00 },
                    { 0x1d, 0x03 },
                    { 0x52, 0x0f },
                    { 0x53, 0x13 },
                    { 0x54, 0x17 },
                    { 0x55, 0x03 },
                    { 0x56, 0x07 },
                    { 0x57, 0x0b },
                    { 0x58, 0x83 },
                    { 0x59, 0x00 },
                    { 0x5a, 0xc1 },
                    { 0x5b, 0x00 },
                    { 0x5c, 0x00 },
                    { 0x5d, 0x00 },
                    { 0x5e, 0x00 }
                }
            },
            { { 300 }, 11000, 2, {
                    { 0x08, 0x00 },
                    { 0x09, 0x05 },
                    { 0x0a, 0x06 },
                    { 0x0b, 0x08 },
                    { 0x16, 0x0b },
                    { 0x17, 0x0a },
                    { 0x18, 0x28 },
                    { 0x19, 0x2a },
                    { 0x1a, 0x00 },
                    { 0x1b, 0x00 },
                    { 0x1c, 0x00 },
                    { 0x1d, 0x03 },
                    { 0x52, 0x0f },
                    { 0x53, 0x13 },
                    { 0x54, 0x17 },
                    { 0x55, 0x03 },
                    { 0x56, 0x07 },
                    { 0x57, 0x0b },
                    { 0x58, 0x83 },
                    { 0x59, 0x00 },
                    { 0x5a, 0xc1 },
                    { 0x5b, 0x00 },
                    { 0x5c, 0x00 },
                    { 0x5d, 0x00 },
                    { 0x5e, 0x00 }
                }
            },
            { { 400 }, 11000, 2, {
                    { 0x08, 0x00 },
                    { 0x09, 0x05 },
                    { 0x0a, 0x06 },
                    { 0x0b, 0x08 },
                    { 0x16, 0x0b },
                    { 0x17, 0x0a },
                    { 0x18, 0x28 },
                    { 0x19, 0x2a },
                    { 0x1a, 0x00 },
                    { 0x1b, 0x00 },
                    { 0x1c, 0x00 },
                    { 0x1d, 0x03 },
                    { 0x52, 0x0f },
                    { 0x53, 0x13 },
                    { 0x54, 0x17 },
                    { 0x55, 0x03 },
                    { 0x56, 0x07 },
                    { 0x57, 0x0b },
                    { 0x58, 0x83 },
                    { 0x59, 0x00 },
                    { 0x5a, 0xc1 },
                    { 0x5b, 0x00 },
                    { 0x5c, 0x00 },
                    { 0x5d, 0x00 },
                    { 0x5e, 0x00 }
                }
            },
            { { 600 }, 11000, 2, {
                    { 0x08, 0x00 },
                    { 0x09, 0x05 },
                    { 0x0a, 0x06 },
                    { 0x0b, 0x08 },
                    { 0x16, 0x0b },
                    { 0x17, 0x0a },
                    { 0x18, 0x28 },
                    { 0x19, 0x2a },
                    { 0x1a, 0x00 },
                    { 0x1b, 0x00 },
                    { 0x1c, 0x00 },
                    { 0x1d, 0x03 },
                    { 0x52, 0x0f },
                    { 0x53, 0x13 },
                    { 0x54, 0x17 },
                    { 0x55, 0x03 },
                    { 0x56, 0x07 },
                    { 0x57, 0x0b },
                    { 0x58, 0x83 },
                    { 0x59, 0x00 },
                    { 0x5a, 0xc1 },
                    { 0x5b, 0x00 },
                    { 0x5c, 0x00 },
                    { 0x5d, 0x00 },
                    { 0x5e, 0x00 }
                }
            },
            { { 1200 }, 11000, 1, {
                    { 0x08, 0x0d },
                    { 0x09, 0x0f },
                    { 0x0a, 0x11 },
                    { 0x0b, 0x13 },
                    { 0x16, 0x0b },
                    { 0x17, 0x0a },
                    { 0x18, 0x30 },
                    { 0x19, 0x2a },
                    { 0x1a, 0x00 },
                    { 0x1b, 0x00 },
                    { 0x1c, 0x00 },
                    { 0x1d, 0x03 },
                    { 0x52, 0x03 },
                    { 0x53, 0x07 },
                    { 0x54, 0x0b },
                    { 0x55, 0x0f },
                    { 0x56, 0x13 },
                    { 0x57, 0x17 },
                    { 0x58, 0x23 },
                    { 0x59, 0x00 },
                    { 0x5a, 0xc1 },
                    { 0x5b, 0x00 },
                    { 0x5c, 0x00 },
                    { 0x5d, 0x00 },
                    { 0x5e, 0x00 }
                }
            },
        };

        for (const CustomSensorSettings& setting : custom_settings)
        {
            sensor.resolutions = setting.resolutions;
            sensor.exposure_lperiod = setting.exposure_lperiod;
            sensor.ccd_size_divisor = setting.ccd_size_divisor;
            sensor.custom_regs = setting.custom_regs;
            s_sensors->push_back(sensor);
        }
    }


    sensor = Genesys_Sensor();
    sensor.sensor_id = SensorId::CCD_HP2400;
    sensor.optical_res = 1200;
    sensor.black_pixels = 48;
    sensor.dummy_pixel = 15;
    sensor.ccd_start_xoffset = 0;
    sensor.fau_gain_white_ref = 210;
    sensor.gain_white_ref = 200;
    sensor.exposure = { 0x0000, 0x0000, 0x0000 };
    sensor.stagger_config = StaggerConfig{1200, 4}; // FIXME: may be incorrect
    sensor.custom_base_regs = {
        { 0x08, 0x14 },
        { 0x09, 0x15 },
        { 0x0a, 0x00 },
        { 0x0b, 0x00 },
        { 0x16, 0xbf },
        { 0x17, 0x08 },
        { 0x18, 0x3f },
        { 0x19, 0x2a },
        { 0x1a, 0x00 },
        { 0x1b, 0x00 },
        { 0x1c, 0x00 },
        { 0x1d, 0x02 },
        { 0x52, 0x0b },
        { 0x53, 0x0f },
        { 0x54, 0x13 },
        { 0x55, 0x17 },
        { 0x56, 0x03 },
        { 0x57, 0x07 },
        { 0x58, 0x63 },
        { 0x59, 0x00 },
        { 0x5a, 0xc1 },
        { 0x5b, 0x00 },
        { 0x5c, 0x0e },
        { 0x5d, 0x00 },
        { 0x5e, 0x00 },
    };
    sensor.gamma = { 2.1f, 2.1f, 2.1f };
    sensor.get_register_hwdpi_fun = default_get_register_hwdpi;
    sensor.get_hwdpi_divisor_fun = default_get_hwdpi_divisor_for_dpi;
    sensor.get_ccd_size_divisor_fun = get_ccd_size_divisor_exact;

    {
        struct CustomSensorSettings {
            ResolutionFilter resolutions;
            unsigned exposure_lperiod;
            GenesysRegisterSettingSet custom_regs;
        };

        CustomSensorSettings custom_settings[] = {
            { { 50 }, 7211, {
                    { 0x08, 0x14 },
                    { 0x09, 0x15 },
                    { 0x0a, 0x00 },
                    { 0x0b, 0x00 },
                    { 0x16, 0xbf },
                    { 0x17, 0x08 },
                    { 0x18, 0x3f },
                    { 0x19, 0x2a },
                    { 0x1a, 0x00 },
                    { 0x1b, 0x00 },
                    { 0x1c, 0x00 },
                    { 0x1d, 0x02 },
                    { 0x52, 0x0b },
                    { 0x53, 0x0f },
                    { 0x54, 0x13 },
                    { 0x55, 0x17 },
                    { 0x56, 0x03 },
                    { 0x57, 0x07 },
                    { 0x58, 0x63 },
                    { 0x59, 0x00 },
                    { 0x5a, 0xc1 },
                    { 0x5b, 0x00 },
                    { 0x5c, 0x00 },
                    { 0x5d, 0x00 },
                    { 0x5e, 0x00 }
                }
            },
            { { 100 }, 7211, {
                    { 0x08, 0x14 },
                    { 0x09, 0x15 },
                    { 0x0a, 0x00 },
                    { 0x0b, 0x00 },
                    { 0x16, 0xbf },
                    { 0x17, 0x08 },
                    { 0x18, 0x3f },
                    { 0x19, 0x2a },
                    { 0x1a, 0x00 },
                    { 0x1b, 0x00 },
                    { 0x1c, 0x00 },
                    { 0x1d, 0x02 },
                    { 0x52, 0x0b },
                    { 0x53, 0x0f },
                    { 0x54, 0x13 },
                    { 0x55, 0x17 },
                    { 0x56, 0x03 },
                    { 0x57, 0x07 },
                    { 0x58, 0x63 },
                    { 0x59, 0x00 },
                    { 0x5a, 0xc1 },
                    { 0x5b, 0x00 },
                    { 0x5c, 0x00 },
                    { 0x5d, 0x00 },
                    { 0x5e, 0x00 }
                }
            },
            { { 150 }, 7211, {
                    { 0x08, 0x14 },
                    { 0x09, 0x15 },
                    { 0x0a, 0x00 },
                    { 0x0b, 0x00 },
                    { 0x16, 0xbf },
                    { 0x17, 0x08 },
                    { 0x18, 0x3f },
                    { 0x19, 0x2a },
                    { 0x1a, 0x00 },
                    { 0x1b, 0x00 },
                    { 0x1c, 0x00 },
                    { 0x1d, 0x02 },
                    { 0x52, 0x0b },
                    { 0x53, 0x0f },
                    { 0x54, 0x13 },
                    { 0x55, 0x17 },
                    { 0x56, 0x03 },
                    { 0x57, 0x07 },
                    { 0x58, 0x63 },
                    { 0x59, 0x00 },
                    { 0x5a, 0xc1 },
                    { 0x5b, 0x00 },
                    { 0x5c, 0x00 },
                    { 0x5d, 0x00 },
                    { 0x5e, 0x00 }
                }
            },
            { { 300 }, 8751, {
                    { 0x08, 0x14 },
                    { 0x09, 0x15 },
                    { 0x0a, 0x00 },
                    { 0x0b, 0x00 },
                    { 0x16, 0xbf },
                    { 0x17, 0x08 },
                    { 0x18, 0x3f },
                    { 0x19, 0x2a },
                    { 0x1a, 0x00 },
                    { 0x1b, 0x00 },
                    { 0x1c, 0x00 },
                    { 0x1d, 0x02 },
                    { 0x52, 0x0b },
                    { 0x53, 0x0f },
                    { 0x54, 0x13 },
                    { 0x55, 0x17 },
                    { 0x56, 0x03 },
                    { 0x57, 0x07 },
                    { 0x58, 0x63 },
                    { 0x59, 0x00 },
                    { 0x5a, 0xc1 },
                    { 0x5b, 0x00 },
                    { 0x5c, 0x00 },
                    { 0x5d, 0x00 },
                    { 0x5e, 0x00 }
                }
            },
            { { 600 }, 18760, {
                    { 0x08, 0x0e },
                    { 0x09, 0x0f },
                    { 0x0a, 0x00 },
                    { 0x0b, 0x00 },
                    { 0x16, 0xbf },
                    { 0x17, 0x08 },
                    { 0x18, 0x31 },
                    { 0x19, 0x2a },
                    { 0x1a, 0x00 },
                    { 0x1b, 0x00 },
                    { 0x1c, 0x00 },
                    { 0x1d, 0x02 },
                    { 0x52, 0x03 },
                    { 0x53, 0x07 },
                    { 0x54, 0x0b },
                    { 0x55, 0x0f },
                    { 0x56, 0x13 },
                    { 0x57, 0x17 },
                    { 0x58, 0x23 },
                    { 0x59, 0x00 },
                    { 0x5a, 0xc1 },
                    { 0x5b, 0x00 },
                    { 0x5c, 0x00 },
                    { 0x5d, 0x00 },
                    { 0x5e, 0x00 }
                }
            },
            { { 1200 }, 21749, {
                    { 0x08, 0x02 },
                    { 0x09, 0x04 },
                    { 0x0a, 0x00 },
                    { 0x0b, 0x00 },
                    { 0x16, 0xbf },
                    { 0x17, 0x08 },
                    { 0x18, 0x30 },
                    { 0x19, 0x2a },
                    { 0x1a, 0x00 },
                    { 0x1b, 0x00 },
                    { 0x1c, 0xc0 },
                    { 0x1d, 0x42 },
                    { 0x52, 0x0b },
                    { 0x53, 0x0f },
                    { 0x54, 0x13 },
                    { 0x55, 0x17 },
                    { 0x56, 0x03 },
                    { 0x57, 0x07 },
                    { 0x58, 0x63 },
                    { 0x59, 0x00 },
                    { 0x5a, 0xc1 },
                    { 0x5b, 0x00 },
                    { 0x5c, 0x0e },
                    { 0x5d, 0x00 },
                    { 0x5e, 0x00 }
                }
            },
        };

        for (const CustomSensorSettings& setting : custom_settings)
        {
            sensor.resolutions = setting.resolutions;
            sensor.exposure_lperiod = setting.exposure_lperiod;
            sensor.custom_regs = setting.custom_regs;
            s_sensors->push_back(sensor);
        }
    }


    sensor = Genesys_Sensor();
    sensor.sensor_id = SensorId::CCD_HP2300;
    sensor.optical_res = 600;
    sensor.ccd_size_divisor = 2;
    sensor.black_pixels = 48;
    sensor.dummy_pixel = 20;
    sensor.ccd_start_xoffset = 0;
    sensor.fau_gain_white_ref = 180;
    sensor.gain_white_ref = 180;
    sensor.exposure = { 0x0000, 0x0000, 0x0000 };
    sensor.custom_base_regs = {
        { 0x08, 0x16 },
        { 0x09, 0x00 },
        { 0x0a, 0x01 },
        { 0x0b, 0x03 },
        { 0x16, 0xb7 },
        { 0x17, 0x0a },
        { 0x18, 0x20 },
        { 0x19, 0x2a },
        { 0x1a, 0x6a },
        { 0x1b, 0x8a },
        { 0x1c, 0x00 },
        { 0x1d, 0x05 },
        { 0x52, 0x0f },
        { 0x53, 0x13 },
        { 0x54, 0x17 },
        { 0x55, 0x03 },
        { 0x56, 0x07 },
        { 0x57, 0x0b },
        { 0x58, 0x83 },
        { 0x59, 0x00 },
        { 0x5a, 0xc1 },
        { 0x5b, 0x06 },
        { 0x5c, 0x0b },
        { 0x5d, 0x10 },
        { 0x5e, 0x16 },
    };
    sensor.gamma = { 2.1f, 2.1f, 2.1f };
    sensor.get_register_hwdpi_fun = default_get_register_hwdpi;
    sensor.get_hwdpi_divisor_fun = default_get_hwdpi_divisor_for_dpi;
    sensor.get_ccd_size_divisor_fun = get_ccd_size_divisor_exact;

    {
        struct CustomSensorSettings {
            ResolutionFilter resolutions;
            unsigned exposure_lperiod;
            unsigned ccd_size_divisor;
            GenesysRegisterSettingSet custom_regs;
        };

        CustomSensorSettings custom_settings[] = {
            { { 75 }, 4480, 2, {
                    { 0x08, 0x16 },
                    { 0x09, 0x00 },
                    { 0x0a, 0x01 },
                    { 0x0b, 0x03 },
                    { 0x16, 0xb7 },
                    { 0x17, 0x0a },
                    { 0x18, 0x20 },
                    { 0x19, 0x2a },
                    { 0x1a, 0x6a },
                    { 0x1b, 0x8a },
                    { 0x1c, 0x00 },
                    { 0x1d, 0x85 },
                    { 0x52, 0x0f },
                    { 0x53, 0x13 },
                    { 0x54, 0x17 },
                    { 0x55, 0x03 },
                    { 0x56, 0x07 },
                    { 0x57, 0x0b },
                    { 0x58, 0x83 },
                    { 0x59, 0x00 },
                    { 0x5a, 0xc1 },
                    { 0x5b, 0x06 },
                    { 0x5c, 0x0b },
                    { 0x5d, 0x10 },
                    { 0x5e, 0x16 }
                }
            },
            { { 150 }, 4350, 2, {
                    { 0x08, 0x16 },
                    { 0x09, 0x00 },
                    { 0x0a, 0x01 },
                    { 0x0b, 0x03 },
                    { 0x16, 0xb7 },
                    { 0x17, 0x0a },
                    { 0x18, 0x20 },
                    { 0x19, 0x2a },
                    { 0x1a, 0x6a },
                    { 0x1b, 0x8a },
                    { 0x1c, 0x00 },
                    { 0x1d, 0x85 },
                    { 0x52, 0x0f },
                    { 0x53, 0x13 },
                    { 0x54, 0x17 },
                    { 0x55, 0x03 },
                    { 0x56, 0x07 },
                    { 0x57, 0x0b },
                    { 0x58, 0x83 },
                    { 0x59, 0x00 },
                    { 0x5a, 0xc1 },
                    { 0x5b, 0x06 },
                    { 0x5c, 0x0b },
                    { 0x5d, 0x10 },
                    { 0x5e, 0x16 }
                }
            },
            { { 300 }, 4350, 2, {
                    { 0x08, 0x16 },
                    { 0x09, 0x00 },
                    { 0x0a, 0x01 },
                    { 0x0b, 0x03 },
                    { 0x16, 0xb7 },
                    { 0x17, 0x0a },
                    { 0x18, 0x20 },
                    { 0x19, 0x2a },
                    { 0x1a, 0x6a },
                    { 0x1b, 0x8a },
                    { 0x1c, 0x00 },
                    { 0x1d, 0x85 },
                    { 0x52, 0x0f },
                    { 0x53, 0x13 },
                    { 0x54, 0x17 },
                    { 0x55, 0x03 },
                    { 0x56, 0x07 },
                    { 0x57, 0x0b },
                    { 0x58, 0x83 },
                    { 0x59, 0x00 },
                    { 0x5a, 0xc1 },
                    { 0x5b, 0x06 },
                    { 0x5c, 0x0b },
                    { 0x5d, 0x10 },
                    { 0x5e, 0x16 }
                }
            },
            { { 600 }, 8700, 1, {
                    { 0x08, 0x01 },
                    { 0x09, 0x03 },
                    { 0x0a, 0x04 },
                    { 0x0b, 0x06 },
                    { 0x16, 0xb7 },
                    { 0x17, 0x0a },
                    { 0x18, 0x20 },
                    { 0x19, 0x2a },
                    { 0x1a, 0x6a },
                    { 0x1b, 0x8a },
                    { 0x1c, 0x00 },
                    { 0x1d, 0x05 },
                    { 0x52, 0x0f },
                    { 0x53, 0x13 },
                    { 0x54, 0x17 },
                    { 0x55, 0x03 },
                    { 0x56, 0x07 },
                    { 0x57, 0x0b },
                    { 0x58, 0x83 },
                    { 0x59, 0x00 },
                    { 0x5a, 0xc1 },
                    { 0x5b, 0x06 },
                    { 0x5c, 0x0b },
                    { 0x5d, 0x10 },
                    { 0x5e, 0x16 }
                }
            },
        };

        for (const CustomSensorSettings& setting : custom_settings)
        {
            sensor.resolutions = setting.resolutions;
            sensor.exposure_lperiod = setting.exposure_lperiod;
            sensor.ccd_size_divisor = setting.ccd_size_divisor;
            sensor.custom_regs = setting.custom_regs;
            s_sensors->push_back(sensor);
        }
    }


    sensor = Genesys_Sensor();
    sensor.sensor_id = SensorId::CIS_CANON_LIDE_35;
    sensor.optical_res = 1200;
    sensor.ccd_size_divisor = 2;
    sensor.black_pixels = 87;
    sensor.dummy_pixel = 87;
    sensor.ccd_start_xoffset = 0;
    sensor.fau_gain_white_ref = 0;
    sensor.gain_white_ref = 0;
    sensor.exposure = { 0x0400, 0x0400, 0x0400 };
    sensor.custom_regs = {
        { 0x08, 0x00 },
        { 0x09, 0x00 },
        { 0x0a, 0x00 },
        { 0x0b, 0x00 },
        { 0x16, 0x00 },
        { 0x17, 0x02 },
        { 0x18, 0x00 },
        { 0x19, 0x50 },
        { 0x1a, 0x00 }, // TODO: 1a-1d: these do no harm, but may be neccessery for CCD
        { 0x1b, 0x00 },
        { 0x1c, 0x00 },
        { 0x1d, 0x02 },
        { 0x52, 0x05 }, // [GB](HI|LOW) not needed for cis
        { 0x53, 0x07 },
        { 0x54, 0x00 },
        { 0x55, 0x00 },
        { 0x56, 0x00 },
        { 0x57, 0x00 },
        { 0x58, 0x3a },
        { 0x59, 0x03 },
        { 0x5a, 0x40 },
        { 0x5b, 0x00 }, // TODO: 5b-5e
        { 0x5c, 0x00 },
        { 0x5d, 0x00 },
        { 0x5e, 0x00 },
    };
    sensor.gamma = { 1.0f, 1.0f, 1.0f };
    sensor.get_register_hwdpi_fun = default_get_register_hwdpi;
    sensor.get_hwdpi_divisor_fun = default_get_hwdpi_divisor_for_dpi;
    sensor.get_ccd_size_divisor_fun = default_get_ccd_size_divisor_for_dpi;
    s_sensors->push_back(sensor);


    sensor = Genesys_Sensor();
    sensor.sensor_id = SensorId::CIS_XP200;
    sensor.optical_res = 600;
    sensor.black_pixels = 5;
    sensor.dummy_pixel = 38;
    sensor.ccd_start_xoffset = 0;
    sensor.fau_gain_white_ref = 200;
    sensor.gain_white_ref = 200;
    sensor.exposure = { 0x1450, 0x0c80, 0x0a28 };
    sensor.custom_base_regs = {
        { 0x08, 0x16 },
        { 0x09, 0x00 },
        { 0x0a, 0x01 },
        { 0x0b, 0x03 },
        { 0x16, 0xb7 },
        { 0x17, 0x0a },
        { 0x18, 0x20 },
        { 0x19, 0x2a },
        { 0x1a, 0x6a },
        { 0x1b, 0x8a },
        { 0x1c, 0x00 },
        { 0x1d, 0x05 },
        { 0x52, 0x0f },
        { 0x53, 0x13 },
        { 0x54, 0x17 },
        { 0x55, 0x03 },
        { 0x56, 0x07 },
        { 0x57, 0x0b },
        { 0x58, 0x83 },
        { 0x59, 0x00 },
        { 0x5a, 0xc1 },
        { 0x5b, 0x06 },
        { 0x5c, 0x0b },
        { 0x5d, 0x10 },
        { 0x5e, 0x16 },
    };
    sensor.custom_regs = {
        { 0x08, 0x06 },
        { 0x09, 0x07 },
        { 0x0a, 0x0a },
        { 0x0b, 0x04 },
        { 0x16, 0x24 },
        { 0x17, 0x04 },
        { 0x18, 0x00 },
        { 0x19, 0x2a },
        { 0x1a, 0x0a },
        { 0x1b, 0x0a },
        { 0x1c, 0x00 },
        { 0x1d, 0x11 },
        { 0x52, 0x08 },
        { 0x53, 0x02 },
        { 0x54, 0x00 },
        { 0x55, 0x00 },
        { 0x56, 0x00 },
        { 0x57, 0x00 },
        { 0x58, 0x1a },
        { 0x59, 0x51 },
        { 0x5a, 0x00 },
        { 0x5b, 0x00 },
        { 0x5c, 0x00 },
        { 0x5d, 0x00 },
        { 0x5e, 0x00 }
    };
    sensor.gamma = { 2.1f, 2.1f, 2.1f };
    sensor.get_register_hwdpi_fun = default_get_register_hwdpi;
    sensor.get_hwdpi_divisor_fun = default_get_hwdpi_divisor_for_dpi;
    sensor.get_ccd_size_divisor_fun = get_ccd_size_divisor_exact;

    {
        struct CustomSensorSettings {
            ResolutionFilter resolutions;
            std::vector<unsigned> channels;
            unsigned exposure_lperiod;
            SensorExposure exposure;
        };

        CustomSensorSettings custom_settings[] = {
            {  { 75 }, { 3 },  5700, { 0x1644, 0x0c80, 0x092e } },
            { { 100 }, { 3 },  5700, { 0x1644, 0x0c80, 0x092e } },
            { { 200 }, { 3 },  5700, { 0x1644, 0x0c80, 0x092e } },
            { { 300 }, { 3 },  9000, { 0x1644, 0x0c80, 0x092e } },
            { { 600 }, { 3 }, 16000, { 0x1644, 0x0c80, 0x092e } },
            {  { 75 }, { 1 }, 16000, { 0x050a, 0x0fa0, 0x1010 } },
            { { 100 }, { 1 },  7800, { 0x050a, 0x0fa0, 0x1010 } },
            { { 200 }, { 1 }, 11000, { 0x050a, 0x0fa0, 0x1010 } },
            { { 300 }, { 1 }, 13000, { 0x050a, 0x0fa0, 0x1010 } },
            { { 600 }, { 1 }, 24000, { 0x050a, 0x0fa0, 0x1010 } },
        };

        for (const CustomSensorSettings& setting : custom_settings)
        {
            sensor.resolutions = setting.resolutions;
            sensor.channels = setting.channels;
            sensor.exposure_lperiod = setting.exposure_lperiod;
            sensor.exposure = setting.exposure;
            s_sensors->push_back(sensor);
        }
    }


    sensor = Genesys_Sensor();
    sensor.sensor_id = SensorId::CCD_HP3670;
    sensor.optical_res = 1200;
    sensor.black_pixels = 48;
    sensor.dummy_pixel = 16;
    sensor.ccd_start_xoffset = 0;
    sensor.fau_gain_white_ref = 210;
    sensor.gain_white_ref = 200;
    sensor.exposure = { 0, 0, 0 };
    sensor.stagger_config = StaggerConfig{1200, 4}; // FIXME: may be incorrect
    sensor.custom_base_regs = {
        { 0x08, 0x00 },
        { 0x09, 0x0a },
        { 0x0a, 0x0b },
        { 0x0b, 0x0d },
        { 0x16, 0x33 },
        { 0x17, 0x07 },
        { 0x18, 0x20 },
        { 0x19, 0x2a },
        { 0x1a, 0x00 },
        { 0x1b, 0x00 },
        { 0x1c, 0xc0 },
        { 0x1d, 0x43 },
        { 0x52, 0x0f },
        { 0x53, 0x13 },
        { 0x54, 0x17 },
        { 0x55, 0x03 },
        { 0x56, 0x07 },
        { 0x57, 0x0b },
        { 0x58, 0x83 },
        { 0x59, 0x00 },
        { 0x5a, 0x15 },
        { 0x5b, 0x05 },
        { 0x5c, 0x0a },
        { 0x5d, 0x0f },
        { 0x5e, 0x00 },
    };
    sensor.gamma = { 1.0f, 1.0f, 1.0f };
    sensor.get_register_hwdpi_fun = default_get_register_hwdpi;
    sensor.get_hwdpi_divisor_fun = default_get_hwdpi_divisor_for_dpi;
    sensor.get_ccd_size_divisor_fun = get_ccd_size_divisor_exact;

    {
        struct CustomSensorSettings {
            ResolutionFilter resolutions;
            unsigned exposure_lperiod;
            GenesysRegisterSettingSet custom_regs;
        };

        CustomSensorSettings custom_settings[] = {
            { { 50 }, 5758, {
                    { 0x08, 0x00 },
                    { 0x09, 0x0a },
                    { 0x0a, 0x0b },
                    { 0x0b, 0x0d },
                    { 0x16, 0x33 },
                    { 0x17, 0x07 },
                    { 0x18, 0x33 },
                    { 0x19, 0x2a },
                    { 0x1a, 0x02 },
                    { 0x1b, 0x13 },
                    { 0x1c, 0xc0 },
                    { 0x1d, 0x43 },
                    { 0x52, 0x0f },
                    { 0x53, 0x13 },
                    { 0x54, 0x17 },
                    { 0x55, 0x03 },
                    { 0x56, 0x07 },
                    { 0x57, 0x0b },
                    { 0x58, 0x83 },
                    { 0x59, 0x15 },
                    { 0x5a, 0xc1 },
                    { 0x5b, 0x05 },
                    { 0x5c, 0x0a },
                    { 0x5d, 0x0f },
                    { 0x5e, 0x00 }
                }
            },
            { { 75 }, 4879, {
                    { 0x08, 0x00 },
                    { 0x09, 0x0a },
                    { 0x0a, 0x0b },
                    { 0x0b, 0x0d },
                    { 0x16, 0x33 },
                    { 0x17, 0x07 },
                    { 0x18, 0x33 },
                    { 0x19, 0x2a },
                    { 0x1a, 0x02 },
                    { 0x1b, 0x13 },
                    { 0x1c, 0xc0 },
                    { 0x1d, 0x43 },
                    { 0x52, 0x0f },
                    { 0x53, 0x13 },
                    { 0x54, 0x17 },
                    { 0x55, 0x03 },
                    { 0x56, 0x07 },
                    { 0x57, 0x0b },
                    { 0x58, 0x83 },
                    { 0x59, 0x15 },
                    { 0x5a, 0xc1 },
                    { 0x5b, 0x05 },
                    { 0x5c, 0x0a },
                    { 0x5d, 0x0f },
                    { 0x5e, 0x00 }
                }
            },
            { { 100 }, 4487, {
                    { 0x08, 0x00 },
                    { 0x09, 0x0a },
                    { 0x0a, 0x0b },
                    { 0x0b, 0x0d },
                    { 0x16, 0x33 },
                    { 0x17, 0x07 },
                    { 0x18, 0x33 },
                    { 0x19, 0x2a },
                    { 0x1a, 0x02 },
                    { 0x1b, 0x13 },
                    { 0x1c, 0xc0 },
                    { 0x1d, 0x43 },
                    { 0x52, 0x0f },
                    { 0x53, 0x13 },
                    { 0x54, 0x17 },
                    { 0x55, 0x03 },
                    { 0x56, 0x07 },
                    { 0x57, 0x0b },
                    { 0x58, 0x83 },
                    { 0x59, 0x15 },
                    { 0x5a, 0xc1 },
                    { 0x5b, 0x05 },
                    { 0x5c, 0x0a },
                    { 0x5d, 0x0f },
                    { 0x5e, 0x00 }
                }
            },
            { { 150 }, 4879, {
                    { 0x08, 0x00 },
                    { 0x09, 0x0a },
                    { 0x0a, 0x0b },
                    { 0x0b, 0x0d },
                    { 0x16, 0x33 },
                    { 0x17, 0x07 },
                    { 0x18, 0x33 },
                    { 0x19, 0x2a },
                    { 0x1a, 0x02 },
                    { 0x1b, 0x13 },
                    { 0x1c, 0xc0 },
                    { 0x1d, 0x43 },
                    { 0x52, 0x0f },
                    { 0x53, 0x13 },
                    { 0x54, 0x17 },
                    { 0x55, 0x03 },
                    { 0x56, 0x07 },
                    { 0x57, 0x0b },
                    { 0x58, 0x83 },
                    { 0x59, 0x15 },
                    { 0x5a, 0xc1 },
                    { 0x5b, 0x05 },
                    { 0x5c, 0x0a },
                    { 0x5d, 0x0f },
                    { 0x5e, 0x00 }
                }
            },
            { { 300 }, 4503, {
                    { 0x08, 0x00 },
                    { 0x09, 0x0a },
                    { 0x0a, 0x0b },
                    { 0x0b, 0x0d },
                    { 0x16, 0x33 },
                    { 0x17, 0x07 },
                    { 0x18, 0x33 },
                    { 0x19, 0x2a },
                    { 0x1a, 0x02 },
                    { 0x1b, 0x13 },
                    { 0x1c, 0xc0 },
                    { 0x1d, 0x43 },
                    { 0x52, 0x0f },
                    { 0x53, 0x13 },
                    { 0x54, 0x17 },
                    { 0x55, 0x03 },
                    { 0x56, 0x07 },
                    { 0x57, 0x0b },
                    { 0x58, 0x83 },
                    { 0x59, 0x15 },
                    { 0x5a, 0xc1 },
                    { 0x5b, 0x05 },
                    { 0x5c, 0x0a },
                    { 0x5d, 0x0f },
                    { 0x5e, 0x00 }
                }
            },
            { { 600 }, 10251, {
                    { 0x08, 0x00 },
                    { 0x09, 0x05 },
                    { 0x0a, 0x06 },
                    { 0x0b, 0x08 },
                    { 0x16, 0x33 },
                    { 0x17, 0x07 },
                    { 0x18, 0x31 },
                    { 0x19, 0x2a },
                    { 0x1a, 0x02 },
                    { 0x1b, 0x0e },
                    { 0x1c, 0xc0 },
                    { 0x1d, 0x43 },
                    { 0x52, 0x0b },
                    { 0x53, 0x0f },
                    { 0x54, 0x13 },
                    { 0x55, 0x17 },
                    { 0x56, 0x03 },
                    { 0x57, 0x07 },
                    { 0x58, 0x63 },
                    { 0x59, 0x00 },
                    { 0x5a, 0xc1 },
                    { 0x5b, 0x02 },
                    { 0x5c, 0x0e },
                    { 0x5d, 0x00 },
                    { 0x5e, 0x00 }
                }
            },
            { { 1200 }, 12750, {
                    { 0x08, 0x0d },
                    { 0x09, 0x0f },
                    { 0x0a, 0x11 },
                    { 0x0b, 0x13 },
                    { 0x16, 0x2b },
                    { 0x17, 0x07 },
                    { 0x18, 0x30 },
                    { 0x19, 0x2a },
                    { 0x1a, 0x00 },
                    { 0x1b, 0x00 },
                    { 0x1c, 0xc0 },
                    { 0x1d, 0x43 },
                    { 0x52, 0x03 },
                    { 0x53, 0x07 },
                    { 0x54, 0x0b },
                    { 0x55, 0x0f },
                    { 0x56, 0x13 },
                    { 0x57, 0x17 },
                    { 0x58, 0x23 },
                    { 0x59, 0x00 },
                    { 0x5a, 0xc1 },
                    { 0x5b, 0x00 },
                    { 0x5c, 0x00 },
                    { 0x5d, 0x00 },
                    { 0x5e, 0x00 }
                }
            },
        };

        for (const CustomSensorSettings& setting : custom_settings)
        {
            sensor.resolutions = setting.resolutions;
            sensor.exposure_lperiod = setting.exposure_lperiod;
            sensor.custom_regs = setting.custom_regs;
            s_sensors->push_back(sensor);
        }
    }


    sensor = Genesys_Sensor();
    sensor.sensor_id = SensorId::CCD_DP665;
    sensor.optical_res = 600;
    sensor.black_pixels = 27;
    sensor.dummy_pixel = 27;
    sensor.ccd_start_xoffset = 0;
    sensor.fau_gain_white_ref = 210;
    sensor.gain_white_ref = 200;
    sensor.exposure = { 0x1100, 0x1100, 0x1100 };
    sensor.custom_regs = {
        { 0x08, 0x00 },
        { 0x09, 0x00 },
        { 0x0a, 0x00 },
        { 0x0b, 0x00 },
        { 0x16, 0x00 },
        { 0x17, 0x02 },
        { 0x18, 0x04 },
        { 0x19, 0x50 },
        { 0x1a, 0x10 },
        { 0x1b, 0x00 },
        { 0x1c, 0x20 },
        { 0x1d, 0x02 },
        { 0x52, 0x04 }, // [GB](HI|LOW) not needed for cis
        { 0x53, 0x05 },
        { 0x54, 0x00 },
        { 0x55, 0x00 },
        { 0x56, 0x00 },
        { 0x57, 0x00 },
        { 0x58, 0x54 },
        { 0x59, 0x03 },
        { 0x5a, 0x00 },
        { 0x5b, 0x00 }, // TODO: 5b-5e
        { 0x5c, 0x00 },
        { 0x5d, 0x00 },
        { 0x5e, 0x01 },
    };
    sensor.gamma = { 1.0f, 1.0f, 1.0f };
    sensor.get_register_hwdpi_fun = default_get_register_hwdpi;
    sensor.get_hwdpi_divisor_fun = default_get_hwdpi_divisor_for_dpi;
    sensor.get_ccd_size_divisor_fun = default_get_ccd_size_divisor_for_dpi;
    s_sensors->push_back(sensor);


    sensor = Genesys_Sensor();
    sensor.sensor_id = SensorId::CCD_ROADWARRIOR;
    sensor.optical_res = 600;
    sensor.black_pixels = 27;
    sensor.dummy_pixel = 27;
    sensor.ccd_start_xoffset = 0;
    sensor.fau_gain_white_ref = 210;
    sensor.gain_white_ref = 200;
    sensor.exposure = { 0x1100, 0x1100, 0x1100 };
    sensor.custom_regs = {
        { 0x08, 0x00 },
        { 0x09, 0x00 },
        { 0x0a, 0x00 },
        { 0x0b, 0x00 },
        { 0x16, 0x00 },
        { 0x17, 0x02 },
        { 0x18, 0x04 },
        { 0x19, 0x50 },
        { 0x1a, 0x10 },
        { 0x1b, 0x00 },
        { 0x1c, 0x20 },
        { 0x1d, 0x02 },
        { 0x52, 0x04 }, // [GB](HI|LOW) not needed for cis
        { 0x53, 0x05 },
        { 0x54, 0x00 },
        { 0x55, 0x00 },
        { 0x56, 0x00 },
        { 0x57, 0x00 },
        { 0x58, 0x54 },
        { 0x59, 0x03 },
        { 0x5a, 0x00 },
        { 0x5b, 0x00 }, // TODO: 5b-5e
        { 0x5c, 0x00 },
        { 0x5d, 0x00 },
        { 0x5e, 0x01 },
    };
    sensor.gamma = { 1.0f, 1.0f, 1.0f };
    sensor.get_register_hwdpi_fun = default_get_register_hwdpi;
    sensor.get_hwdpi_divisor_fun = default_get_hwdpi_divisor_for_dpi;
    sensor.get_ccd_size_divisor_fun = default_get_ccd_size_divisor_for_dpi;
    s_sensors->push_back(sensor);


    sensor = Genesys_Sensor();
    sensor.sensor_id = SensorId::CCD_DSMOBILE600;
    sensor.optical_res = 600;
    sensor.black_pixels = 28;
    sensor.dummy_pixel = 28;
    sensor.ccd_start_xoffset = 0;
    sensor.fau_gain_white_ref = 210;
    sensor.gain_white_ref = 200;
    sensor.exposure = { 0x1544, 0x1544, 0x1544 };
    sensor.custom_regs = {
        { 0x08, 0x00 },
        { 0x09, 0x00 },
        { 0x0a, 0x00 },
        { 0x0b, 0x00 },
        { 0x16, 0x00 },
        { 0x17, 0x02 },
        { 0x18, 0x04 },
        { 0x19, 0x50 },
        { 0x1a, 0x10 },
        { 0x1b, 0x00 },
        { 0x1c, 0x20 },
        { 0x1d, 0x02 },
        { 0x52, 0x04 }, // [GB](HI|LOW) not needed for cis
        { 0x53, 0x05 },
        { 0x54, 0x00 },
        { 0x55, 0x00 },
        { 0x56, 0x00 },
        { 0x57, 0x00 },
        { 0x58, 0x54 },
        { 0x59, 0x03 },
        { 0x5a, 0x00 },
        { 0x5b, 0x00 }, // TODO: 5b-5e
        { 0x5c, 0x00 },
        { 0x5d, 0x00 },
        { 0x5e, 0x01 },
    };
    sensor.gamma = { 1.0f, 1.0f, 1.0f };
    sensor.get_register_hwdpi_fun = default_get_register_hwdpi;
    sensor.get_hwdpi_divisor_fun = default_get_hwdpi_divisor_for_dpi;
    sensor.get_ccd_size_divisor_fun = default_get_ccd_size_divisor_for_dpi;
    s_sensors->push_back(sensor);


    sensor = Genesys_Sensor();
    sensor.sensor_id = SensorId::CCD_XP300;
    sensor.optical_res = 600;
    sensor.black_pixels = 27;
    sensor.dummy_pixel = 27;
    sensor.ccd_start_xoffset = 0;
    sensor.fau_gain_white_ref = 210;
    sensor.gain_white_ref = 200;
    sensor.exposure = { 0x1100, 0x1100, 0x1100 };
    sensor.custom_regs = {
        { 0x08, 0x00 },
        { 0x09, 0x00 },
        { 0x0a, 0x00 },
        { 0x0b, 0x00 },
        { 0x16, 0x00 },
        { 0x17, 0x02 },
        { 0x18, 0x04 },
        { 0x19, 0x50 },
        { 0x1a, 0x10 },
        { 0x1b, 0x00 },
        { 0x1c, 0x20 },
        { 0x1d, 0x02 },
        { 0x52, 0x04 }, // [GB](HI|LOW) not needed for cis
        { 0x53, 0x05 },
        { 0x54, 0x00 },
        { 0x55, 0x00 },
        { 0x56, 0x00 },
        { 0x57, 0x00 },
        { 0x58, 0x54 },
        { 0x59, 0x03 },
        { 0x5a, 0x00 },
        { 0x5b, 0x00 }, // TODO: 5b-5e
        { 0x5c, 0x00 },
        { 0x5d, 0x00 },
        { 0x5e, 0x01 },
    };
    sensor.gamma = { 1.0f, 1.0f, 1.0f };
    sensor.get_register_hwdpi_fun = default_get_register_hwdpi;
    sensor.get_hwdpi_divisor_fun = default_get_hwdpi_divisor_for_dpi;
    sensor.get_ccd_size_divisor_fun = default_get_ccd_size_divisor_for_dpi;
    s_sensors->push_back(sensor);


    sensor = Genesys_Sensor();
    sensor.sensor_id = SensorId::CCD_DP685;
    sensor.optical_res = 600;
    sensor.black_pixels = 27;
    sensor.dummy_pixel = 27;
    sensor.ccd_start_xoffset = 0;
    sensor.fau_gain_white_ref = 210;
    sensor.gain_white_ref = 200;
    sensor.exposure = { 0x1100, 0x1100, 0x1100 };
    sensor.custom_regs = {
        { 0x08, 0x00 },
        { 0x09, 0x00 },
        { 0x0a, 0x00 },
        { 0x0b, 0x00 },
        { 0x16, 0x00 },
        { 0x17, 0x02 },
        { 0x18, 0x04 },
        { 0x19, 0x50 },
        { 0x1a, 0x10 },
        { 0x1b, 0x00 },
        { 0x1c, 0x20 },
        { 0x1d, 0x02 },
        { 0x52, 0x04 }, // [GB](HI|LOW) not needed for cis
        { 0x53, 0x05 },
        { 0x54, 0x00 },
        { 0x55, 0x00 },
        { 0x56, 0x00 },
        { 0x57, 0x00 },
        { 0x58, 0x54 },
        { 0x59, 0x03 },
        { 0x5a, 0x00 },
        { 0x5b, 0x00 }, // TODO: 5b-5e
        { 0x5c, 0x00 },
        { 0x5d, 0x00 },
        { 0x5e, 0x01 },
    };
    sensor.gamma = { 1.0f, 1.0f, 1.0f };
    sensor.get_register_hwdpi_fun = default_get_register_hwdpi;
    sensor.get_hwdpi_divisor_fun = default_get_hwdpi_divisor_for_dpi;
    sensor.get_ccd_size_divisor_fun = default_get_ccd_size_divisor_for_dpi;
    s_sensors->push_back(sensor);


    sensor = Genesys_Sensor();
    sensor.sensor_id = SensorId::CIS_CANON_LIDE_200;
    sensor.optical_res = 4800;
    sensor.black_pixels = 87*4;
    sensor.dummy_pixel = 16*4;
    sensor.ccd_start_xoffset = 320*8;
    sensor.fau_gain_white_ref = 210;
    sensor.gain_white_ref = 200;
    sensor.exposure = { 0x0000, 0x0000, 0x0000 };
    sensor.gamma = { 2.2f, 2.2f, 2.2f };
    sensor.get_register_hwdpi_fun = default_get_register_hwdpi;
    sensor.get_hwdpi_divisor_fun = default_get_hwdpi_divisor_for_dpi;
    sensor.get_ccd_size_divisor_fun = default_get_ccd_size_divisor_for_dpi;

    {
        struct CustomSensorSettings {
            ResolutionFilter resolutions;
            int exposure_lperiod;
            SensorExposure exposure;
            unsigned segment_size;
            std::vector<unsigned> segment_order;
            GenesysRegisterSettingSet custom_regs;
        };

        CustomSensorSettings custom_settings[] = {
            // Note: Windows driver uses 1424 lperiod and enables dummy line (0x17)
            {   { 75, 100, 150, 200 }, 2848, { 304, 203, 180 }, 5136, std::vector<unsigned>{}, {
                    { 0x16, 0x10 }, { 0x17, 0x0a }, { 0x18, 0x00 }, { 0x19, 0xff },
                    { 0x1a, 0x34 }, { 0x1b, 0x00 }, { 0x1c, 0x02 }, { 0x1d, 0x04 },
                    { 0x52, 0x03 }, { 0x53, 0x07 }, { 0x54, 0x00 }, { 0x55, 0x00 },
                    { 0x56, 0x00 }, { 0x57, 0x00 }, { 0x58, 0x2a }, { 0x59, 0xe1 }, { 0x5a, 0x55 },
                    { 0x74, 0x00 }, { 0x75, 0x00 }, { 0x76, 0x3c },
                    { 0x77, 0x00 }, { 0x78, 0x00 }, { 0x79, 0x9f },
                    { 0x7a, 0x00 }, { 0x7b, 0x00 }, { 0x7c, 0x55 },
                }
            },
            // Note: Windows driver uses 788 lperiod and enables dummy line (0x17)
            {   { 300, 400 }, 1424, { 304, 203, 180 }, 5136, std::vector<unsigned>{}, {
                    { 0x16, 0x10 }, { 0x17, 0x0a }, { 0x18, 0x00 }, { 0x19, 0xff },
                    { 0x1a, 0x34 }, { 0x1b, 0x00 }, { 0x1c, 0x02 }, { 0x1d, 0x04 },
                    { 0x52, 0x03 }, { 0x53, 0x07 }, { 0x54, 0x00 }, { 0x55, 0x00 },
                    { 0x56, 0x00 }, { 0x57, 0x00 }, { 0x58, 0x2a }, { 0x59, 0xe1 }, { 0x5a, 0x55 },
                    { 0x74, 0x00 }, { 0x75, 0x00 }, { 0x76, 0x3c },
                    { 0x77, 0x00 }, { 0x78, 0x00 }, { 0x79, 0x9f },
                    { 0x7a, 0x00 }, { 0x7b, 0x00 }, { 0x7c, 0x55 },
                }
            },
            {   { 600 }, 1432, { 492, 326, 296 }, 5136, std::vector<unsigned>{}, {
                    { 0x16, 0x10 }, { 0x17, 0x0a }, { 0x18, 0x00 }, { 0x19, 0xff },
                    { 0x1a, 0x34 }, { 0x1b, 0x00 }, { 0x1c, 0x02 }, { 0x1d, 0x04 },
                    { 0x52, 0x03 }, { 0x53, 0x07 }, { 0x54, 0x00 }, { 0x55, 0x00 },
                    { 0x56, 0x00 }, { 0x57, 0x00 }, { 0x58, 0x2a }, { 0x59, 0xe1 }, { 0x5a, 0x55 },
                    { 0x74, 0x00 }, { 0x75, 0x00 }, { 0x76, 0x3c },
                    { 0x77, 0x00 }, { 0x78, 0x00 }, { 0x79, 0x9f },
                    { 0x7a, 0x00 }, { 0x7b, 0x00 }, { 0x7c, 0x55 },
                }
            },
            {   { 1200 }, 2712, { 935, 592, 538 }, 5136, { 0, 1 }, {
                    { 0x16, 0x10 }, { 0x17, 0x08 }, { 0x18, 0x00 }, { 0x19, 0xff },
                    { 0x1a, 0x34 }, { 0x1b, 0x00 }, { 0x1c, 0x02 }, { 0x1d, 0x04 },
                    { 0x52, 0x03 }, { 0x53, 0x07 }, { 0x54, 0x00 }, { 0x55, 0x00 },
                    { 0x56, 0x00 }, { 0x57, 0x00 }, { 0x58, 0x2a }, { 0x59, 0xe1 }, { 0x5a, 0x55 },
                    { 0x74, 0x00 }, { 0x75, 0x00 }, { 0x76, 0x3c },
                    { 0x77, 0x00 }, { 0x78, 0x00 }, { 0x79, 0x9f },
                    { 0x7a, 0x00 }, { 0x7b, 0x00 }, { 0x7c, 0x55 },
                }
            },
            {   { 2400 }, 5280, { 1777, 1125, 979 }, 5136, { 0, 2, 1, 3 }, {
                    { 0x16, 0x10 }, { 0x17, 0x06 }, { 0x18, 0x00 }, { 0x19, 0xff },
                    { 0x1a, 0x34 }, { 0x1b, 0x00 }, { 0x1c, 0x02 }, { 0x1d, 0x04 },
                    { 0x52, 0x03 }, { 0x53, 0x07 }, { 0x54, 0x00 }, { 0x55, 0x00 },
                    { 0x56, 0x00 }, { 0x57, 0x00 }, { 0x58, 0x2a }, { 0x59, 0xe1 }, { 0x5a, 0x55 },
                    { 0x74, 0x00 }, { 0x75, 0x00 }, { 0x76, 0x3c },
                    { 0x77, 0x00 }, { 0x78, 0x00 }, { 0x79, 0x9f },
                    { 0x7a, 0x00 }, { 0x7b, 0x00 }, { 0x7c, 0x55 },
                }
            },
            {   { 4800 }, 10416, { 3377, 2138, 1780 }, 5136, { 0, 2, 4, 6, 1, 3, 5, 7 }, {
                    { 0x16, 0x10 }, { 0x17, 0x04 }, { 0x18, 0x00 }, { 0x19, 0xff },
                    { 0x1a, 0x34 }, { 0x1b, 0x00 }, { 0x1c, 0x02 }, { 0x1d, 0x04 },
                    { 0x52, 0x03 }, { 0x53, 0x07 }, { 0x54, 0x00 }, { 0x55, 0x00 },
                    { 0x56, 0x00 }, { 0x57, 0x00 }, { 0x58, 0x2a }, { 0x59, 0xe1 }, { 0x5a, 0x55 },
                    { 0x74, 0x00 }, { 0x75, 0x00 }, { 0x76, 0x3c },
                    { 0x77, 0x00 }, { 0x78, 0x00 }, { 0x79, 0x9f },
                    { 0x7a, 0x00 }, { 0x7b, 0x00 }, { 0x7c, 0x55 },
                }
            }
        };

        for (const auto& setting : custom_settings) {
            sensor.resolutions = setting.resolutions;
            sensor.exposure_lperiod = setting.exposure_lperiod;
            sensor.exposure = setting.exposure;
            sensor.segment_size = setting.segment_size;
            sensor.segment_order = setting.segment_order;
            sensor.custom_regs = setting.custom_regs;
            s_sensors->push_back(sensor);
        }
    }


    sensor = Genesys_Sensor();
    sensor.sensor_id = SensorId::CIS_CANON_LIDE_700F;
    sensor.optical_res = 4800;
    sensor.black_pixels = 73*8; // black pixels 73 at 600 dpi
    sensor.dummy_pixel = 16*8;
    // 384 at 600 dpi
    sensor.ccd_start_xoffset = 384*8;
    sensor.fau_gain_white_ref = 210;
    sensor.gain_white_ref = 200;
    sensor.gamma = { 1.0f, 1.0f, 1.0f };
    sensor.get_register_hwdpi_fun = default_get_register_hwdpi;
    sensor.get_hwdpi_divisor_fun = default_get_hwdpi_divisor_for_dpi;
    sensor.get_ccd_size_divisor_fun = default_get_ccd_size_divisor_for_dpi;

    {
        struct CustomSensorSettings {
            ResolutionFilter resolutions;
            int exposure_lperiod;
            SensorExposure exposure;
            unsigned segment_size;
            std::vector<unsigned> segment_order;
            GenesysRegisterSettingSet custom_regs;
        };

        CustomSensorSettings custom_settings[] = {
            {   { 75, 100, 150, 200 }, 2848, { 465, 310, 239 }, 5187, std::vector<unsigned>{}, {
                    { 0x16, 0x10 }, { 0x17, 0x0c }, { 0x18, 0x00 }, { 0x19, 0xff },
                    { 0x1a, 0x34 }, { 0x1b, 0x00 }, { 0x1c, 0x02 }, { 0x1d, 0x04 },
                    { 0x52, 0x07 }, { 0x53, 0x03 }, { 0x54, 0x00 }, { 0x55, 0x00 },
                    { 0x56, 0x00 }, { 0x57, 0x00 }, { 0x58, 0x2a }, { 0x59, 0xe1 }, { 0x5a, 0x55 },
                    { 0x74, 0x00 }, { 0x75, 0x00 }, { 0x76, 0x87 },
                    { 0x77, 0x00 }, { 0x78, 0x00 }, { 0x79, 0xf9 },
                    { 0x7a, 0x00 }, { 0x7b, 0x00 }, { 0x7c, 0x55 },
                }
            },
            {   { 300 }, 1424, { 465, 310, 239 }, 5187, std::vector<unsigned>{}, {
                    { 0x16, 0x10 }, { 0x17, 0x0c }, { 0x18, 0x00 }, { 0x19, 0xff },
                    { 0x1a, 0x34 }, { 0x1b, 0x00 }, { 0x1c, 0x02 }, { 0x1d, 0x04 },
                    { 0x52, 0x07 }, { 0x53, 0x03 }, { 0x54, 0x00 }, { 0x55, 0x00 },
                    { 0x56, 0x00 }, { 0x57, 0x00 }, { 0x58, 0x2a }, { 0x59, 0xe1 }, { 0x5a, 0x55 },
                    { 0x74, 0x00 }, { 0x75, 0x00 }, { 0x76, 0x87 },
                    { 0x77, 0x00 }, { 0x78, 0x00 }, { 0x79, 0xf9 },
                    { 0x7a, 0x00 }, { 0x7b, 0x00 }, { 0x7c, 0x55 },
                }
            },
            {   { 600 }, 1504, { 465, 310, 239 }, 5187, std::vector<unsigned>{}, {
                    { 0x16, 0x10 }, { 0x17, 0x0c }, { 0x18, 0x00 }, { 0x19, 0xff },
                    { 0x1a, 0x34 }, { 0x1b, 0x00 }, { 0x1c, 0x02 }, { 0x1d, 0x04 },
                    { 0x52, 0x07 }, { 0x53, 0x03 }, { 0x54, 0x00 }, { 0x55, 0x00 },
                    { 0x56, 0x00 }, { 0x57, 0x00 }, { 0x58, 0x2a }, { 0x59, 0xe1 }, { 0x5a, 0x55 },
                    { 0x74, 0x00 }, { 0x75, 0x00 }, { 0x76, 0x87 },
                    { 0x77, 0x00 }, { 0x78, 0x00 }, { 0x79, 0xf9 },
                    { 0x7a, 0x00 }, { 0x7b, 0x00 }, { 0x7c, 0x55 },
                }
            },
            {   { 1200 }, 2696, { 1464, 844, 555 }, 5187, { 0, 1 }, {
                    { 0x16, 0x10 }, { 0x17, 0x0a }, { 0x18, 0x00 }, { 0x19, 0xff },
                    { 0x1a, 0x34 }, { 0x1b, 0x00 }, { 0x1c, 0x02 }, { 0x1d, 0x04 },
                    { 0x52, 0x07 }, { 0x53, 0x03 }, { 0x54, 0x00 }, { 0x55, 0x00 },
                    { 0x56, 0x00 }, { 0x57, 0x00 }, { 0x58, 0x2a }, { 0x59, 0xe1 }, { 0x5a, 0x55 },
                    { 0x74, 0x00 }, { 0x75, 0x00 }, { 0x76, 0x87 },
                    { 0x77, 0x00 }, { 0x78, 0x00 }, { 0x79, 0xf9 },
                    { 0x7a, 0x00 }, { 0x7b, 0x00 }, { 0x7c, 0x55 },
                }
            },
            {   { 2400 }, 10576, { 2798, 1558, 972 }, 5187, { 0, 1, 2, 3 }, {
                    { 0x16, 0x10 }, { 0x17, 0x08 }, { 0x18, 0x00 }, { 0x19, 0xff },
                    { 0x1a, 0x34 }, { 0x1b, 0x00 }, { 0x1c, 0x02 }, { 0x1d, 0x04 },
                    { 0x52, 0x07 }, { 0x53, 0x03 }, { 0x54, 0x00 }, { 0x55, 0x00 },
                    { 0x56, 0x00 }, { 0x57, 0x00 }, { 0x58, 0x2a }, { 0x59, 0xe1 }, { 0x5a, 0x55 },
                    { 0x74, 0x00 }, { 0x75, 0x00 }, { 0x76, 0x87 },
                    { 0x77, 0x00 }, { 0x78, 0x00 }, { 0x79, 0xf9 },
                    { 0x7a, 0x00 }, { 0x7b, 0x00 }, { 0x7c, 0x55 },
                }
            },
            {   { 4800 }, 10576, { 2798, 1558, 972 }, 5187, { 0, 1, 4, 5, 2, 3, 6, 7 }, {
                    { 0x16, 0x10 }, { 0x17, 0x06 }, { 0x18, 0x00 }, { 0x19, 0xff },
                    { 0x1a, 0x34 }, { 0x1b, 0x00 }, { 0x1c, 0x02 }, { 0x1d, 0x04 },
                    { 0x52, 0x07 }, { 0x53, 0x03 }, { 0x54, 0x00 }, { 0x55, 0x00 },
                    { 0x56, 0x00 }, { 0x57, 0x00 }, { 0x58, 0x2a }, { 0x59, 0xe1 }, { 0x5a, 0x55 },
                    { 0x74, 0x00 }, { 0x75, 0x00 }, { 0x76, 0x87 },
                    { 0x77, 0x00 }, { 0x78, 0x00 }, { 0x79, 0xf9 },
                    { 0x7a, 0x00 }, { 0x7b, 0x00 }, { 0x7c, 0x55 },
                }
            }
        };

        for (const auto& setting : custom_settings) {
            sensor.resolutions = setting.resolutions;
            sensor.exposure_lperiod = setting.exposure_lperiod;
            sensor.exposure = setting.exposure;
            sensor.segment_size = setting.segment_size;
            sensor.segment_order = setting.segment_order;
            sensor.custom_regs = setting.custom_regs;
            s_sensors->push_back(sensor);
        }
    }


    sensor = Genesys_Sensor();
    sensor.sensor_id = SensorId::CIS_CANON_LIDE_100;
    sensor.optical_res = 2400;
    sensor.black_pixels = 87*4;
    sensor.dummy_pixel = 16*4;
    sensor.ccd_start_xoffset = 320*4;
    sensor.fau_gain_white_ref = 210;
    sensor.gain_white_ref = 200;
    sensor.exposure = { 0x01c1, 0x0126, 0x00e5 };
    sensor.gamma = { 2.2f, 2.2f, 2.2f };
    sensor.get_register_hwdpi_fun = default_get_register_hwdpi;
    sensor.get_hwdpi_divisor_fun = default_get_hwdpi_divisor_for_dpi;
    sensor.get_ccd_size_divisor_fun = default_get_ccd_size_divisor_for_dpi;

    {
        struct CustomSensorSettings {
            ResolutionFilter resolutions;
            int exposure_lperiod;
            SensorExposure exposure;
            unsigned segment_size;
            std::vector<unsigned> segment_order;
            GenesysRegisterSettingSet custom_regs;
        };

        CustomSensorSettings custom_settings[] = {
            {   { 75, 100, 150, 200 }, 2304, { 423, 294, 242 }, 5136, std::vector<unsigned>{}, {
                    { 0x16, 0x10 }, { 0x17, 0x0a }, { 0x18, 0x00 }, { 0x19, 0xff },
                    { 0x1a, 0x34 }, { 0x1b, 0x00 }, { 0x1c, 0x02 }, { 0x1d, 0x04 },
                    { 0x52, 0x03 }, { 0x53, 0x07 }, { 0x54, 0x00 }, { 0x55, 0x00 },
                    { 0x56, 0x00 }, { 0x57, 0x00 }, { 0x58, 0x2a }, { 0x59, 0xe1 }, { 0x5a, 0x55 },
                    { 0x74, 0x00 }, { 0x75, 0x00 }, { 0x76, 0x3c },
                    { 0x77, 0x00 }, { 0x78, 0x00 }, { 0x79, 0x9f },
                    { 0x7a, 0x00 }, { 0x7b, 0x00 }, { 0x7c, 0x55 },
                }
            },
            {   { 300 }, 1728, { 423, 294, 242 }, 5136, std::vector<unsigned>{}, {
                    { 0x16, 0x10 }, { 0x17, 0x0a }, { 0x18, 0x00 }, { 0x19, 0xff },
                    { 0x1a, 0x34 }, { 0x1b, 0x00 }, { 0x1c, 0x02 }, { 0x1d, 0x04 },
                    { 0x52, 0x03 }, { 0x53, 0x07 }, { 0x54, 0x00 }, { 0x55, 0x00 },
                    { 0x56, 0x00 }, { 0x57, 0x00 }, { 0x58, 0x2a }, { 0x59, 0xe1 }, { 0x5a, 0x55 },
                    { 0x74, 0x00 }, { 0x75, 0x00 }, { 0x76, 0x3c },
                    { 0x77, 0x00 }, { 0x78, 0x00 }, { 0x79, 0x9f },
                    { 0x7a, 0x00 }, { 0x7b, 0x00 }, { 0x7c, 0x55 },
                }
            },
            {   { 600 }, 1432, { 423, 294, 242 }, 5136, std::vector<unsigned>{}, {
                    { 0x16, 0x10 }, { 0x17, 0x0a }, { 0x18, 0x00 }, { 0x19, 0xff },
                    { 0x1a, 0x34 }, { 0x1b, 0x00 }, { 0x1c, 0x02 }, { 0x1d, 0x04 },
                    { 0x52, 0x03 }, { 0x53, 0x07 }, { 0x54, 0x00 }, { 0x55, 0x00 },
                    { 0x56, 0x00 }, { 0x57, 0x00 }, { 0x58, 0x2a }, { 0x59, 0xe1 }, { 0x5a, 0x55 },
                    { 0x74, 0x00 }, { 0x75, 0x00 }, { 0x76, 0x3c },
                    { 0x77, 0x00 }, { 0x78, 0x00 }, { 0x79, 0x9f },
                    { 0x7a, 0x00 }, { 0x7b, 0x00 }, { 0x7c, 0x55 },
                },
            },
            {   { 1200 }, 2712, { 791, 542, 403 }, 5136, {0, 1}, {
                    { 0x16, 0x10 }, { 0x17, 0x08 }, { 0x18, 0x00 }, { 0x19, 0xff },
                    { 0x1a, 0x34 }, { 0x1b, 0x00 }, { 0x1c, 0x02 }, { 0x1d, 0x04 },
                    { 0x52, 0x03 }, { 0x53, 0x07 }, { 0x54, 0x00 }, { 0x55, 0x00 },
                    { 0x56, 0x00 }, { 0x57, 0x00 }, { 0x58, 0x2a }, { 0x59, 0xe1 }, { 0x5a, 0x55 },
                    { 0x74, 0x00 }, { 0x75, 0x00 }, { 0x76, 0x3c },
                    { 0x77, 0x00 }, { 0x78, 0x00 }, { 0x79, 0x9f },
                    { 0x7a, 0x00 }, { 0x7b, 0x00 }, { 0x7c, 0x55 },
                }
            },
            {   { 2400 }, 5280, { 1504, 1030, 766 }, 5136, {0, 2, 1, 3}, {
                    { 0x16, 0x10 }, { 0x17, 0x06 }, { 0x18, 0x00 }, { 0x19, 0xff },
                    { 0x1a, 0x34 }, { 0x1b, 0x00 }, { 0x1c, 0x02 }, { 0x1d, 0x04 },
                    { 0x52, 0x03 }, { 0x53, 0x07 }, { 0x54, 0x00 }, { 0x55, 0x00 },
                    { 0x56, 0x00 }, { 0x57, 0x00 }, { 0x58, 0x2a }, { 0x59, 0xe1 }, { 0x5a, 0x55 },
                    { 0x74, 0x00 }, { 0x75, 0x00 }, { 0x76, 0x3c },
                    { 0x77, 0x00 }, { 0x78, 0x00 }, { 0x79, 0x9f },
                    { 0x7a, 0x00 }, { 0x7b, 0x00 }, { 0x7c, 0x55 },
                }
            }
        };

        for (const auto& setting : custom_settings) {
            sensor.resolutions = setting.resolutions;
            sensor.exposure_lperiod = setting.exposure_lperiod;
            sensor.exposure = setting.exposure;
            sensor.segment_size = setting.segment_size;
            sensor.segment_order = setting.segment_order;
            sensor.custom_regs = setting.custom_regs;
            s_sensors->push_back(sensor);
        }
    }

    sensor = Genesys_Sensor();
    sensor.sensor_id = SensorId::CCD_KVSS080;
    sensor.optical_res = 600;
    sensor.black_pixels = 38;
    sensor.dummy_pixel = 38;
    sensor.ccd_start_xoffset = 152;
    sensor.fau_gain_white_ref = 160;
    sensor.gain_white_ref = 160;
    sensor.exposure = { 0x0000, 0x0000, 0x0000 };
    sensor.exposure_lperiod = 8000;
    sensor.custom_regs = {
        { 0x74, 0x00 }, { 0x75, 0x00 }, { 0x76, 0x00 },
        { 0x77, 0x00 }, { 0x78, 0xff }, { 0x79, 0xff },
        { 0x7a, 0x03 }, { 0x7b, 0xff }, { 0x7c, 0xff },
        { 0x0c, 0x00 },
        { 0x70, 0x01 },
        { 0x71, 0x03 },
        { 0x9e, 0x00 },
        { 0xaa, 0x00 },
        { 0x16, 0x33 },
        { 0x17, 0x1c },
        { 0x18, 0x00 },
        { 0x19, 0x2a },
        { 0x1a, 0x2c },
        { 0x1b, 0x00 },
        { 0x1c, 0x20 },
        { 0x1d, 0x04 },
        { 0x52, 0x0c },
        { 0x53, 0x0f },
        { 0x54, 0x00 },
        { 0x55, 0x03 },
        { 0x56, 0x06 },
        { 0x57, 0x09 },
        { 0x58, 0x6b },
        { 0x59, 0x00 },
        { 0x5a, 0xc0 },
    };
    sensor.gamma = { 1.0f, 1.0f, 1.0f };
    sensor.get_register_hwdpi_fun = default_get_register_hwdpi;
    sensor.get_hwdpi_divisor_fun = default_get_hwdpi_divisor_for_dpi;
    sensor.get_ccd_size_divisor_fun = default_get_ccd_size_divisor_for_dpi;
    s_sensors->push_back(sensor);


    sensor = Genesys_Sensor();
    sensor.sensor_id = SensorId::CCD_G4050;
    sensor.optical_res = 4800;
    sensor.black_pixels = 50*8;
    // 31 at 600 dpi dummy_pixels 58 at 1200
    sensor.dummy_pixel = 58;
    sensor.ccd_start_xoffset = 152;
    sensor.fau_gain_white_ref = 160;
    sensor.gain_white_ref = 160;
    sensor.exposure = { 0x2c09, 0x22b8, 0x10f0 };
    sensor.stagger_config = StaggerConfig{ 2400, 4 }; // FIXME: may be incorrect
    sensor.custom_regs = {};
    sensor.gamma = { 1.0f, 1.0f, 1.0f };
    sensor.get_register_hwdpi_fun = default_get_register_hwdpi;
    sensor.get_hwdpi_divisor_fun = default_get_hwdpi_divisor_for_dpi;
    sensor.get_ccd_size_divisor_fun = default_get_ccd_size_divisor_for_dpi;

    {
        struct CustomSensorSettings {
            ResolutionFilter resolutions;
            int exposure_lperiod;
            ScanMethod method;
            GenesysRegisterSettingSet extra_custom_regs;
        };

        CustomSensorSettings custom_settings[] = {
            { { 100, 150, 200, 300, 400, 600 }, 8016, ScanMethod::FLATBED, {
                    { 0x74, 0x00 }, { 0x75, 0x01 }, { 0x76, 0xff },
                    { 0x77, 0x03 }, { 0x78, 0xff }, { 0x79, 0xff },
                    { 0x7a, 0x03 }, { 0x7b, 0xff }, { 0x7c, 0xff },
                    { 0x0c, 0x00 },
                    { 0x70, 0x00 },
                    { 0x71, 0x02 },
                    { 0x9e, 0x00 },
                    { 0xaa, 0x00 },
                    { 0x16, 0x33 },
                    { 0x17, 0x0c },
                    { 0x18, 0x00 },
                    { 0x19, 0x2a },
                    { 0x1a, 0x30 },
                    { 0x1b, 0x00 },
                    { 0x1c, 0x00 },
                    { 0x1d, 0x08 },
                    { 0x52, 0x0b },
                    { 0x53, 0x0e },
                    { 0x54, 0x11 },
                    { 0x55, 0x02 },
                    { 0x56, 0x05 },
                    { 0x57, 0x08 },
                    { 0x58, 0x63 },
                    { 0x59, 0x00 },
                    { 0x5a, 0x40 },
                }
            },
            { { 1200 }, 56064, ScanMethod::FLATBED, {
                    { 0x74, 0x0f }, { 0x75, 0xff }, { 0x76, 0xff },
                    { 0x77, 0x00 }, { 0x78, 0x01 }, { 0x79, 0xff },
                    { 0x7a, 0x00 }, { 0x7b, 0x01 }, { 0x7c, 0xff },
                    { 0x0c, 0x20 },
                    { 0x70, 0x08 },
                    { 0x71, 0x0c },
                    { 0x9e, 0xc0 },
                    { 0xaa, 0x05 },
                    { 0x16, 0x3b },
                    { 0x17, 0x0c },
                    { 0x18, 0x10 },
                    { 0x19, 0x2a },
                    { 0x1a, 0x38 },
                    { 0x1b, 0x10 },
                    { 0x1c, 0x00 },
                    { 0x1d, 0x08 },
                    { 0x52, 0x02 },
                    { 0x53, 0x05 },
                    { 0x54, 0x08 },
                    { 0x55, 0x0b },
                    { 0x56, 0x0e },
                    { 0x57, 0x11 },
                    { 0x58, 0x1b },
                    { 0x59, 0x00 },
                    { 0x5a, 0x40 },
                }
            },
            { { 2400 }, 56064, ScanMethod::FLATBED, {
                    { 0x74, 0x0f }, { 0x75, 0xff }, { 0x76, 0xff },
                    { 0x77, 0x00 }, { 0x78, 0x00 }, { 0x79, 0x00 },
                    { 0x7a, 0x00 }, { 0x7b, 0x00 }, { 0x7c, 0x00 },
                    { 0x0c, 0x20 },
                    { 0x70, 0x08 },
                    { 0x71, 0x0a },
                    { 0x9e, 0xc0 },
                    { 0xaa, 0x05 },
                    { 0x16, 0x3b },
                    { 0x17, 0x0c },
                    { 0x18, 0x10 },
                    { 0x19, 0x2a },
                    { 0x1a, 0x38 },
                    { 0x1b, 0x10 },
                    { 0x1c, 0xc0 },
                    { 0x1d, 0x08 },
                    { 0x52, 0x02 },
                    { 0x53, 0x05 },
                    { 0x54, 0x08 },
                    { 0x55, 0x0b },
                    { 0x56, 0x0e },
                    { 0x57, 0x11 },
                    { 0x58, 0x1b },
                    { 0x59, 0x00 },
                    { 0x5a, 0x40 },
                }
            },
            { { 4800 }, 42752, ScanMethod::FLATBED, {
                    { 0x74, 0x0f }, { 0x75, 0xff }, { 0x76, 0xff },
                    { 0x77, 0x00 }, { 0x78, 0x00 }, { 0x79, 0x00 },
                    { 0x7a, 0x00 }, { 0x7b, 0x00 }, { 0x7c, 0x00 },
                    { 0x0c, 0x21 },
                    { 0x70, 0x08 },
                    { 0x71, 0x0a },
                    { 0x9e, 0xc0 },
                    { 0xaa, 0x07 },
                    { 0x16, 0x3b },
                    { 0x17, 0x0c },
                    { 0x18, 0x10 },
                    { 0x19, 0x2a },
                    { 0x1a, 0x38 },
                    { 0x1b, 0x10 },
                    { 0x1c, 0xc1 },
                    { 0x1d, 0x08 },
                    { 0x52, 0x02 },
                    { 0x53, 0x05 },
                    { 0x54, 0x08 },
                    { 0x55, 0x0b },
                    { 0x56, 0x0e },
                    { 0x57, 0x11 },
                    { 0x58, 0x1b },
                    { 0x59, 0x00 },
                    { 0x5a, 0x40 },
                }
            },
            { ResolutionFilter::ANY, 15624, ScanMethod::TRANSPARENCY, {
                    { 0x74, 0x00 }, { 0x75, 0x1c }, { 0x76, 0x7f },
                    { 0x77, 0x03 }, { 0x78, 0xff }, { 0x79, 0xff },
                    { 0x7a, 0x03 }, { 0x7b, 0xff }, { 0x7c, 0xff },
                    { 0x0c, 0x00 },
                    { 0x70, 0x00 },
                    { 0x71, 0x02 },
                    { 0x9e, 0x00 },
                    { 0xaa, 0x00 },
                    { 0x16, 0x33 },
                    { 0x17, 0x4c },
                    { 0x18, 0x01 },
                    { 0x19, 0x2a },
                    { 0x1a, 0x30 },
                    { 0x1b, 0x00 },
                    { 0x1c, 0x00 },
                    { 0x1d, 0x08 },
                    { 0x52, 0x0e },
                    { 0x53, 0x11 },
                    { 0x54, 0x02 },
                    { 0x55, 0x05 },
                    { 0x56, 0x08 },
                    { 0x57, 0x0b },
                    { 0x58, 0x6b },
                    { 0x59, 0x00 },
                    { 0x5a, 0xc0 },
                }
            }
        };

        auto base_custom_regs = sensor.custom_regs;
        for (const CustomSensorSettings& setting : custom_settings)
        {
            sensor.resolutions = setting.resolutions;
            sensor.exposure_lperiod = setting.exposure_lperiod;
            sensor.method = setting.method;
            sensor.custom_regs = base_custom_regs;
            sensor.custom_regs.merge(setting.extra_custom_regs);
            s_sensors->push_back(sensor);
        }
    }

    sensor = Genesys_Sensor();
    sensor.sensor_id = SensorId::CCD_HP_4850C;
    sensor.optical_res = 4800;
    sensor.black_pixels = 100;
    sensor.dummy_pixel = 58;
    sensor.ccd_start_xoffset = 152;
    sensor.fau_gain_white_ref = 160;
    sensor.gain_white_ref = 160;
    sensor.exposure = { 0x2c09, 0x22b8, 0x10f0 };
    sensor.stagger_config = StaggerConfig{ 2400, 4 }; // FIXME: may be incorrect
    sensor.custom_regs = {};
    sensor.gamma = { 1.0f, 1.0f, 1.0f };
    sensor.get_register_hwdpi_fun = default_get_register_hwdpi;
    sensor.get_hwdpi_divisor_fun = default_get_hwdpi_divisor_for_dpi;
    sensor.get_ccd_size_divisor_fun = default_get_ccd_size_divisor_for_dpi;

    {
        struct CustomSensorSettings {
            ResolutionFilter resolutions;
            int exposure_lperiod;
            ScanMethod method;
            GenesysRegisterSettingSet extra_custom_regs;
        };

        CustomSensorSettings custom_settings[] = {
            {   { 100, 150, 200, 300, 400, 600 }, 8016, ScanMethod::FLATBED, {
                    { 0x0c, 0x00 },
                    { 0x16, 0x33 }, { 0x17, 0x0c }, { 0x18, 0x00 }, { 0x19, 0x2a },
                    { 0x1a, 0x30 }, { 0x1b, 0x00 }, { 0x1c, 0x00 }, { 0x1d, 0x08 },
                    { 0x52, 0x0b }, { 0x53, 0x0e }, { 0x54, 0x11 }, { 0x55, 0x02 },
                    { 0x56, 0x05 }, { 0x57, 0x08 }, { 0x58, 0x63 }, { 0x59, 0x00 }, { 0x5a, 0x40 },
                    { 0x70, 0x00 }, { 0x71, 0x02 },
                    { 0x74, 0x00 }, { 0x75, 0x01 }, { 0x76, 0xff },
                    { 0x77, 0x03 }, { 0x78, 0xff }, { 0x79, 0xff },
                    { 0x7a, 0x03 }, { 0x7b, 0xff }, { 0x7c, 0xff },
                    { 0x9e, 0x00 },
                    { 0xaa, 0x00 },
                }
            },
            {   { 1200 }, 56064, ScanMethod::FLATBED, {
                    { 0x0c, 0x20 },
                    { 0x16, 0x3b }, { 0x17, 0x0c }, { 0x18, 0x10 }, { 0x19, 0x2a },
                    { 0x1a, 0x38 }, { 0x1b, 0x10 }, { 0x1c, 0x00 }, { 0x1d, 0x08 },
                    { 0x52, 0x02 }, { 0x53, 0x05 }, { 0x54, 0x08 }, { 0x55, 0x0b },
                    { 0x56, 0x0e }, { 0x57, 0x11 }, { 0x58, 0x1b }, { 0x59, 0x00 }, { 0x5a, 0x40 },
                    { 0x70, 0x08 }, { 0x71, 0x0c },
                    { 0x74, 0x0f }, { 0x75, 0xff }, { 0x76, 0xff },
                    { 0x77, 0x00 }, { 0x78, 0x01 }, { 0x79, 0xff },
                    { 0x7a, 0x00 }, { 0x7b, 0x01 }, { 0x7c, 0xff },
                    { 0x9e, 0xc0 },
                    { 0xaa, 0x05 },
                }
            },
            {   { 2400 }, 56064, ScanMethod::FLATBED, {
                    { 0x0c, 0x20 },
                    { 0x16, 0x3b }, { 0x17, 0x0c }, { 0x18, 0x10 }, { 0x19, 0x2a },
                    { 0x1a, 0x38 }, { 0x1b, 0x10 }, { 0x1c, 0xc0 }, { 0x1d, 0x08 },
                    { 0x52, 0x02 }, { 0x53, 0x05 }, { 0x54, 0x08 }, { 0x55, 0x0b },
                    { 0x56, 0x0e }, { 0x57, 0x11 }, { 0x58, 0x1b }, { 0x59, 0x00 }, { 0x5a, 0x40 },
                    { 0x70, 0x08 }, { 0x71, 0x0a },
                    { 0x74, 0x0f }, { 0x75, 0xff }, { 0x76, 0xff },
                    { 0x77, 0x00 }, { 0x78, 0x00 }, { 0x79, 0x00 },
                    { 0x7a, 0x00 }, { 0x7b, 0x00 }, { 0x7c, 0x00 },
                    { 0x9e, 0xc0 },
                    { 0xaa, 0x05 },
                }
            },
            {   { 4800 }, 42752, ScanMethod::FLATBED, {
                    { 0x0c, 0x21 },
                    { 0x16, 0x3b }, { 0x17, 0x0c }, { 0x18, 0x10 }, { 0x19, 0x2a },
                    { 0x1a, 0x38 }, { 0x1b, 0x10 }, { 0x1c, 0xc1 }, { 0x1d, 0x08 },
                    { 0x52, 0x02 }, { 0x53, 0x05 }, { 0x54, 0x08 }, { 0x55, 0x0b },
                    { 0x56, 0x0e }, { 0x57, 0x11 }, { 0x58, 0x1b }, { 0x59, 0x00 }, { 0x5a, 0x40 },
                    { 0x70, 0x08 }, { 0x71, 0x0a },
                    { 0x74, 0x0f }, { 0x75, 0xff }, { 0x76, 0xff },
                    { 0x77, 0x00 }, { 0x78, 0x00 }, { 0x79, 0x00 },
                    { 0x7a, 0x00 }, { 0x7b, 0x00 }, { 0x7c, 0x00 },
                    { 0x9e, 0xc0 },
                    { 0xaa, 0x07 },
                }
            },
            {   ResolutionFilter::ANY, 15624, ScanMethod::TRANSPARENCY, {
                    { 0x0c, 0x00 },
                    { 0x16, 0x33 }, { 0x17, 0x4c }, { 0x18, 0x01 }, { 0x19, 0x2a },
                    { 0x1a, 0x30 }, { 0x1b, 0x00 }, { 0x1c, 0x00 }, { 0x1d, 0x08 },
                    { 0x52, 0x0e }, { 0x53, 0x11 }, { 0x54, 0x02 }, { 0x55, 0x05 },
                    { 0x56, 0x08 }, { 0x57, 0x0b }, { 0x58, 0x6b }, { 0x59, 0x00 }, { 0x5a, 0xc0 },
                    { 0x70, 0x00 }, { 0x71, 0x02 },
                    { 0x74, 0x00 }, { 0x75, 0x1c }, { 0x76, 0x7f },
                    { 0x77, 0x03 }, { 0x78, 0xff }, { 0x79, 0xff },
                    { 0x7a, 0x03 }, { 0x7b, 0xff }, { 0x7c, 0xff },
                    { 0x9e, 0x00 },
                    { 0xaa, 0x00 },
                }
            }
        };

        auto base_custom_regs = sensor.custom_regs;
        for (const CustomSensorSettings& setting : custom_settings)
        {
            sensor.resolutions = setting.resolutions;
            sensor.exposure_lperiod = setting.exposure_lperiod;
            sensor.method = setting.method;
            sensor.custom_regs = base_custom_regs;
            sensor.custom_regs.merge(setting.extra_custom_regs);
            s_sensors->push_back(sensor);
        }
    }

    sensor = Genesys_Sensor();
    sensor.sensor_id = SensorId::CCD_CANON_4400F;
    sensor.optical_res = 4800;
    sensor.register_dpihw_override = 4800;
    sensor.ccd_size_divisor = 4;
    sensor.black_pixels = 50*8;
    // 31 at 600 dpi, 58 at 1200 dpi
    sensor.dummy_pixel = 20;
    sensor.ccd_start_xoffset = 152;
    sensor.fau_gain_white_ref = 160;
    sensor.gain_white_ref = 160;
    sensor.exposure = { 0x9c40, 0x9c40, 0x9c40 };
    sensor.stagger_config = StaggerConfig{4800, 8};
    sensor.gamma = { 1.0f, 1.0f, 1.0f };
    sensor.get_register_hwdpi_fun = get_sensor_optical_with_ccd_divisor;
    sensor.get_hwdpi_divisor_fun = [](const Genesys_Sensor&, unsigned) { return 1; };
    sensor.get_ccd_size_divisor_fun = default_get_ccd_size_divisor_for_dpi;

    {
        struct CustomSensorSettings {
            ResolutionFilter resolutions;
            int exposure_lperiod;
            bool use_host_side_calib;
            std::vector<ScanMethod> methods;
            GenesysRegisterSettingSet extra_custom_regs;
            GenesysRegisterSettingSet extra_custom_fe_regs;
        };

        CustomSensorSettings custom_settings[] = {
            {   { 300, 600, 1200 }, 11640, false, { ScanMethod::FLATBED }, {
                    { 0x16, 0x13 }, { 0x17, 0x0a }, { 0x18, 0x10 }, { 0x19, 0x2a },
                    { 0x1a, 0x30 }, { 0x1b, 0x00 }, { 0x1c, 0x00 }, { 0x1d, 0x6b },
                    { 0x52, 0x0a }, { 0x53, 0x0d }, { 0x54, 0x00 }, { 0x55, 0x03 },
                    { 0x56, 0x06 }, { 0x57, 0x08 }, { 0x58, 0x5b }, { 0x59, 0x00 }, { 0x5a, 0x40 },
                    { 0x70, 0x00 }, { 0x71, 0x02 }, { 0x72, 0x01 }, { 0x73, 0x03 },
                    { 0x74, 0x00 }, { 0x75, 0xf8 }, { 0x76, 0x38 },
                    { 0x77, 0x00 }, { 0x78, 0xfc }, { 0x79, 0x00 },
                    { 0x7a, 0x00 }, { 0x7b, 0x92 }, { 0x7c, 0xa4 },
                    { 0x9e, 0x2d },
                }, {}
            },
            {   { 1200 }, 33300, true, { ScanMethod::TRANSPARENCY }, {
                    { 0x16, 0x13 }, { 0x17, 0x0a }, { 0x18, 0x10 }, { 0x19, 0x2a },
                    { 0x1a, 0x30 }, { 0x1b, 0x00 }, { 0x1c, 0x00 }, { 0x1d, 0x6b },
                    { 0x52, 0x0a }, { 0x53, 0x0d }, { 0x54, 0x00 }, { 0x55, 0x03 },
                    { 0x56, 0x06 }, { 0x57, 0x08 }, { 0x58, 0x5b }, { 0x59, 0x00 }, { 0x5a, 0x40 },
                    { 0x70, 0x00 }, { 0x71, 0x02 }, { 0x72, 0x00 }, { 0x73, 0x02 },
                    { 0x74, 0x00 }, { 0x75, 0xf8 }, { 0x76, 0x38 },
                    { 0x77, 0x00 }, { 0x78, 0xfc }, { 0x79, 0x00 },
                    { 0x7a, 0x00 }, { 0x7b, 0x92 }, { 0x7c, 0xa4 },
                    { 0x9e, 0x2d },
                }, {}
            },
            {   { 2400 }, 33300, true, { ScanMethod::TRANSPARENCY }, {
                    { 0x16, 0x13 }, { 0x17, 0x15 }, { 0x18, 0x10 }, { 0x19, 0x2a },
                    { 0x1a, 0x30 }, { 0x1b, 0x00 }, { 0x1c, 0x01 }, { 0x1d, 0x75 },
                    { 0x52, 0x0b }, { 0x53, 0x0d }, { 0x54, 0x00 }, { 0x55, 0x03 },
                    { 0x56, 0x06 }, { 0x57, 0x09 }, { 0x58, 0x53 }, { 0x59, 0x00 }, { 0x5a, 0x40 },
                    { 0x70, 0x00 }, { 0x71, 0x02 }, { 0x72, 0x02 }, { 0x73, 0x04 },
                    { 0x74, 0x00 }, { 0x75, 0xff }, { 0x76, 0x00 },
                    { 0x77, 0x00 }, { 0x78, 0xff }, { 0x79, 0x00 },
                    { 0x7a, 0x00 }, { 0x7b, 0x54 }, { 0x7c, 0x92 },
                    { 0x9e, 0x2d },
                }, {
                    { 0x03, 0x1f },
                }
            },
            {   { 4800 }, 33300, true, { ScanMethod::TRANSPARENCY }, {
                    { 0x16, 0x13 }, { 0x17, 0x15 }, { 0x18, 0x10 }, { 0x19, 0x2a },
                    { 0x1a, 0x30 }, { 0x1b, 0x00 }, { 0x1c, 0x61 }, { 0x1d, 0x75 },
                    { 0x52, 0x02 }, { 0x53, 0x05 }, { 0x54, 0x08 }, { 0x55, 0x0b },
                    { 0x56, 0x0d }, { 0x57, 0x0f }, { 0x58, 0x1b }, { 0x59, 0x00 }, { 0x5a, 0x40 },
                    { 0x70, 0x08 }, { 0x71, 0x0a }, { 0x72, 0x0a }, { 0x73, 0x0c },
                    { 0x74, 0x00 }, { 0x75, 0xff }, { 0x76, 0xff },
                    { 0x77, 0x00 }, { 0x78, 0xff }, { 0x79, 0xff },
                    { 0x7a, 0x00 }, { 0x7b, 0x54 }, { 0x7c, 0x92 },
                    { 0x9e, 0x2d },
                }, {}
            }
        };

        for (const CustomSensorSettings& setting : custom_settings)
        {
            for (auto method : setting.methods) {
                sensor.resolutions = setting.resolutions;
                sensor.exposure_lperiod = setting.exposure_lperiod;
                sensor.use_host_side_calib = setting.use_host_side_calib;
                sensor.method = method;
                sensor.custom_regs = setting.extra_custom_regs;
                sensor.custom_fe_regs = setting.extra_custom_fe_regs;
                s_sensors->push_back(sensor);
            }
        }
    }


    sensor = Genesys_Sensor();
    sensor.sensor_id = SensorId::CCD_CANON_8400F;
    sensor.optical_res = 3200;
    sensor.register_dpihw_override = 4800;
    sensor.ccd_size_divisor = 1;
    sensor.black_pixels = 50*8;
    // 31 at 600 dpi, 58 at 1200 dpi
    sensor.dummy_pixel = 20;
    sensor.ccd_start_xoffset = 152;
    sensor.fau_gain_white_ref = 160;
    sensor.gain_white_ref = 160;
    sensor.exposure = { 0x9c40, 0x9c40, 0x9c40 };
    sensor.stagger_config = StaggerConfig{ 3200, 6 };
    sensor.custom_regs = {};
    sensor.gamma = { 1.0f, 1.0f, 1.0f };
    sensor.get_register_hwdpi_fun = get_sensor_optical_with_ccd_divisor;
    sensor.get_hwdpi_divisor_fun = [](const Genesys_Sensor&, unsigned) { return 1; };
    sensor.get_ccd_size_divisor_fun = default_get_ccd_size_divisor_for_dpi;

    {
        struct CustomSensorSettings {
            ResolutionFilter resolutions;
            unsigned dpiset_override;
            unsigned pixel_count_multiplier;
            int exposure_lperiod;
            std::vector<ScanMethod> methods;
            GenesysRegisterSettingSet extra_custom_regs;
            GenesysRegisterSettingSet custom_fe_regs;
        };

        CustomSensorSettings custom_settings[] = {
            {   { 400 }, 2400, 1, 7200, { ScanMethod::FLATBED }, {
                    { 0x16, 0x33 }, { 0x17, 0x0c }, { 0x18, 0x13 }, { 0x19, 0x2a },
                    { 0x1a, 0x30 }, { 0x1b, 0x00 }, { 0x1c, 0x00 }, { 0x1d, 0x84 }, { 0x1e, 0xa0 },
                    { 0x52, 0x0d }, { 0x53, 0x10 }, { 0x54, 0x01 }, { 0x55, 0x04 },
                    { 0x56, 0x07 }, { 0x57, 0x0a }, { 0x58, 0x6b }, { 0x59, 0x00 }, { 0x5a, 0x40 },
                    { 0x70, 0x01 }, { 0x71, 0x02 }, { 0x72, 0x03 }, { 0x73, 0x04 },
                    { 0x74, 0x00 }, { 0x75, 0x0e }, { 0x76, 0x3f },
                    { 0x77, 0x00 }, { 0x78, 0x00 }, { 0x79, 0x00 },
                    { 0x7a, 0x01 }, { 0x7b, 0xb6 }, { 0x7c, 0xdb },
                    { 0x80, 0x2a },
                }, {}
            },
            {   { 800 }, 4800, 1, 7200, { ScanMethod::FLATBED }, {
                    { 0x16, 0x33 }, { 0x17, 0x0c }, { 0x18, 0x13 }, { 0x19, 0x2a },
                    { 0x1a, 0x30 }, { 0x1b, 0x00 }, { 0x1c, 0x00 }, { 0x1d, 0x84 }, { 0x1e, 0xa0 },
                    { 0x52, 0x0d }, { 0x53, 0x10 }, { 0x54, 0x01 }, { 0x55, 0x04 },
                    { 0x56, 0x07 }, { 0x57, 0x0a }, { 0x58, 0x6b }, { 0x59, 0x00 }, { 0x5a, 0x40 },
                    { 0x70, 0x01 }, { 0x71, 0x02 }, { 0x72, 0x03 }, { 0x73, 0x04 },
                    { 0x74, 0x00 }, { 0x75, 0x0e }, { 0x76, 0x3f },
                    { 0x77, 0x00 }, { 0x78, 0x00 }, { 0x79, 0x00 },
                    { 0x7a, 0x01 }, { 0x7b, 0xb6 }, { 0x7c, 0xdb },
                    { 0x80, 0x20 },
                }, {}
            },
            {   { 1600 }, 4800, 1, 14400, { ScanMethod::FLATBED }, {
                    { 0x16, 0x33 }, { 0x17, 0x0c }, { 0x18, 0x11 }, { 0x19, 0x2a },
                    { 0x1a, 0x30 }, { 0x1b, 0x00 }, { 0x1c, 0x00 }, { 0x1d, 0x84 }, { 0x1e, 0xa1 },
                    { 0x52, 0x0b }, { 0x53, 0x0e }, { 0x54, 0x11 }, { 0x55, 0x02 },
                    { 0x56, 0x05 }, { 0x57, 0x08 }, { 0x58, 0x63 }, { 0x59, 0x00 }, { 0x5a, 0x40 },
                    { 0x70, 0x00 }, { 0x71, 0x01 }, { 0x72, 0x02 }, { 0x73, 0x03 },
                    { 0x74, 0x00 }, { 0x75, 0x01 }, { 0x76, 0xff },
                    { 0x77, 0x00 }, { 0x78, 0x00 }, { 0x79, 0x00 },
                    { 0x7a, 0x02 }, { 0x7b, 0x49 }, { 0x7c, 0x24 },
                    { 0x80, 0x28 },
                }, {
                    { 0x03, 0x1f },
                }
            },
            {   { 3200 }, 4800, 1, 28800, { ScanMethod::FLATBED }, {
                    { 0x16, 0x33 }, { 0x17, 0x0c }, { 0x18, 0x10 }, { 0x19, 0x2a },
                    { 0x1a, 0x30 }, { 0x1b, 0x00 }, { 0x1c, 0x20 }, { 0x1d, 0x84 }, { 0x1e, 0xa1 },
                    { 0x52, 0x02 }, { 0x53, 0x05 }, { 0x54, 0x08 }, { 0x55, 0x0b },
                    { 0x56, 0x0e }, { 0x57, 0x11 }, { 0x58, 0x1b }, { 0x59, 0x00 }, { 0x5a, 0x40 },
                    { 0x70, 0x09 }, { 0x71, 0x0a }, { 0x72, 0x0b }, { 0x73, 0x0c },
                    { 0x74, 0x00 }, { 0x75, 0x00 }, { 0x76, 0x00 },
                    { 0x77, 0x00 }, { 0x78, 0x00 }, { 0x79, 0x00 },
                    { 0x7a, 0x02 }, { 0x7b, 0x49 }, { 0x7c, 0x24 },
                    { 0x80, 0x2b },
                }, {
                    { 0x03, 0x1f },
                },
            },
            {   { 400 }, 2400, 1, 14400, { ScanMethod::TRANSPARENCY,
                                           ScanMethod::TRANSPARENCY_INFRARED }, {
                    { 0x16, 0x33 }, { 0x17, 0x0c }, { 0x18, 0x13 }, { 0x19, 0x2a },
                    { 0x1a, 0x30 }, { 0x1b, 0x00 }, { 0x1c, 0x00 }, { 0x1d, 0x84 }, { 0x1e, 0xa0 },
                    { 0x52, 0x0d }, { 0x53, 0x10 }, { 0x54, 0x01 }, { 0x55, 0x04 },
                    { 0x56, 0x07 }, { 0x57, 0x0a }, { 0x58, 0x6b }, { 0x59, 0x00 }, { 0x5a, 0x40 },
                    { 0x70, 0x01 }, { 0x71, 0x02 }, { 0x72, 0x03 }, { 0x73, 0x04 },
                    { 0x74, 0x00 }, { 0x75, 0x0e }, { 0x76, 0x3f },
                    { 0x77, 0x00 }, { 0x78, 0x00 }, { 0x79, 0x00 },
                    { 0x7a, 0x01 }, { 0x7b, 0xb6 }, { 0x7c, 0xdb },
                    { 0x80, 0x20 },
                }, {}
            },
            {   { 800 }, 4800, 1, 14400, { ScanMethod::TRANSPARENCY,
                                           ScanMethod::TRANSPARENCY_INFRARED }, {
                    { 0x16, 0x33 }, { 0x17, 0x0c }, { 0x18, 0x13 }, { 0x19, 0x2a },
                    { 0x1a, 0x30 }, { 0x1b, 0x00 }, { 0x1c, 0x00 }, { 0x1d, 0x84 }, { 0x1e, 0xa0 },
                    { 0x52, 0x0d }, { 0x53, 0x10 }, { 0x54, 0x01 }, { 0x55, 0x04 },
                    { 0x56, 0x07 }, { 0x57, 0x0a }, { 0x58, 0x6b }, { 0x59, 0x00 }, { 0x5a, 0x40 },
                    { 0x70, 0x00 }, { 0x71, 0x01 }, { 0x72, 0x02 }, { 0x73, 0x03 },
                    { 0x74, 0x00 }, { 0x75, 0x0e }, { 0x76, 0x3f },
                    { 0x77, 0x00 }, { 0x78, 0x00 }, { 0x79, 0x00 },
                    { 0x7a, 0x01 }, { 0x7b, 0xb6 }, { 0x7c, 0xdb },
                    { 0x80, 0x20 },
                }, {}
            },
            {   { 1600 }, 4800, 1, 28800, { ScanMethod::TRANSPARENCY,
                                            ScanMethod::TRANSPARENCY_INFRARED }, {
                    { 0x16, 0x33 }, { 0x17, 0x0c }, { 0x18, 0x11 }, { 0x19, 0x2a },
                    { 0x1a, 0x30 }, { 0x1b, 0x00 }, { 0x1c, 0x00 }, { 0x1d, 0x84 }, { 0x1e, 0xa0 },
                    { 0x52, 0x0b }, { 0x53, 0x0e }, { 0x54, 0x11 }, { 0x55, 0x02 },
                    { 0x56, 0x05 }, { 0x57, 0x08 }, { 0x58, 0x63 }, { 0x59, 0x00 }, { 0x5a, 0x40 },
                    { 0x70, 0x00 }, { 0x71, 0x01 }, { 0x72, 0x02 }, { 0x73, 0x03 },
                    { 0x74, 0x00 }, { 0x75, 0x01 }, { 0x76, 0xff },
                    { 0x77, 0x00 }, { 0x78, 0x00 }, { 0x79, 0x00 },
                    { 0x7a, 0x02 }, { 0x7b, 0x49 }, { 0x7c, 0x24 },
                    { 0x80, 0x29 },
                }, {
                    { 0x03, 0x1f },
                },
            },
            {   { 3200 }, 4800, 1, 28800, { ScanMethod::TRANSPARENCY,
                                            ScanMethod::TRANSPARENCY_INFRARED }, {
                    { 0x16, 0x33 }, { 0x17, 0x0c }, { 0x18, 0x10 }, { 0x19, 0x2a },
                    { 0x1a, 0x30 }, { 0x1b, 0x00 }, { 0x1c, 0x20 }, { 0x1d, 0x84 }, { 0x1e, 0xa0 },
                    { 0x52, 0x02 }, { 0x53, 0x05 }, { 0x54, 0x08 }, { 0x55, 0x0b },
                    { 0x56, 0x0e }, { 0x57, 0x11 }, { 0x58, 0x1b }, { 0x59, 0x00 }, { 0x5a, 0x40 },
                    { 0x70, 0x09 }, { 0x71, 0x0a }, { 0x72, 0x0b }, { 0x73, 0x0c },
                    { 0x74, 0x00 }, { 0x75, 0x00 }, { 0x76, 0x00 },
                    { 0x77, 0x00 }, { 0x78, 0x00 }, { 0x79, 0x00 },
                    { 0x7a, 0x02 }, { 0x7b, 0x49 }, { 0x7c, 0x24 },
                    { 0x80, 0x2b },
                }, {
                    { 0x03, 0x1f },
                },
            },
        };

        for (const CustomSensorSettings& setting : custom_settings)
        {
            for (auto method : setting.methods) {
                sensor.resolutions = setting.resolutions;
                sensor.dpiset_override = setting.dpiset_override;
                sensor.pixel_count_multiplier = setting.pixel_count_multiplier;
                sensor.exposure_lperiod = setting.exposure_lperiod;
                sensor.method = method;
                sensor.custom_regs = setting.extra_custom_regs;
                sensor.custom_fe_regs = setting.custom_fe_regs;
                s_sensors->push_back(sensor);
            }
        }
    }


    sensor = Genesys_Sensor();
    sensor.sensor_id = SensorId::CCD_CANON_8600F;
    sensor.optical_res = 4800;
    sensor.register_dpihw_override = 4800;
    sensor.ccd_size_divisor = 4;
    sensor.black_pixels = 31;
    sensor.dummy_pixel = 20;
    sensor.ccd_start_xoffset = 0; // not used at the moment
    sensor.fau_gain_white_ref = 160;
    sensor.gain_white_ref = 160;
    sensor.exposure = { 0x9c40, 0x9c40, 0x9c40 };
    sensor.stagger_config = StaggerConfig{4800, 8};
    sensor.custom_regs = {};
    sensor.gamma = { 1.0f, 1.0f, 1.0f };
    sensor.get_register_hwdpi_fun = get_sensor_optical_with_ccd_divisor;
    sensor.get_hwdpi_divisor_fun = [](const Genesys_Sensor&, unsigned) { return 1; };
    sensor.get_ccd_size_divisor_fun = default_get_ccd_size_divisor_for_dpi;

    {
        struct CustomSensorSettings {
            ResolutionFilter resolutions;
            int exposure_lperiod;
            std::vector<ScanMethod> methods;
            GenesysRegisterSettingSet extra_custom_regs;
            GenesysRegisterSettingSet custom_fe_regs;
        };

        CustomSensorSettings custom_settings[] = {
            {   { 300, 600, 1200 }, 24000, { ScanMethod::FLATBED }, {
                    { 0x0c, 0x00 },
                    { 0x16, 0x13 }, { 0x17, 0x0a }, { 0x18, 0x10 }, { 0x19, 0x2a },
                    { 0x1a, 0x30 }, { 0x1b, 0x00 }, { 0x1c, 0x00 }, { 0x1d, 0x6b },
                    { 0x52, 0x0c }, { 0x53, 0x0f }, { 0x54, 0x00 }, { 0x55, 0x03 },
                    { 0x70, 0x00 }, { 0x71, 0x02 }, { 0x72, 0x02 }, { 0x73, 0x04 },
                    { 0x56, 0x06 }, { 0x57, 0x09 }, { 0x58, 0x6b }, { 0x59, 0x00 }, { 0x5a, 0x40 },
                    { 0x74, 0x03 }, { 0x75, 0xf0 }, { 0x76, 0xf0 },
                    { 0x77, 0x03 }, { 0x78, 0xfe }, { 0x79, 0x00 },
                    { 0x7a, 0x00 }, { 0x7b, 0x92 }, { 0x7c, 0x49 },
                    { 0x9e, 0x2d },
                    { 0xaa, 0x00 },
                },
                {},
            },
            {   { 300, 600, 1200 }, 45000, { ScanMethod::TRANSPARENCY,
                                             ScanMethod::TRANSPARENCY_INFRARED }, {
                    { 0x0c, 0x00 },
                    { 0x16, 0x13 }, { 0x17, 0x0a }, { 0x18, 0x10 }, { 0x19, 0x2a },
                    { 0x1a, 0x30 }, { 0x1b, 0x00 }, { 0x1c, 0x00 }, { 0x1d, 0x6b },
                    { 0x52, 0x0c }, { 0x53, 0x0f }, { 0x54, 0x00 }, { 0x55, 0x03 },
                    { 0x56, 0x06 }, { 0x57, 0x09 }, { 0x58, 0x6b }, { 0x59, 0x00 }, { 0x5a, 0x40 },
                    { 0x70, 0x00 }, { 0x71, 0x02 }, { 0x72, 0x02 }, { 0x73, 0x04 },
                    { 0x74, 0x03 }, { 0x75, 0xf0 }, { 0x76, 0xf0 },
                    { 0x77, 0x03 }, { 0x78, 0xfe }, { 0x79, 0x00 },
                    { 0x7a, 0x00 }, { 0x7b, 0x92 }, { 0x7c, 0x49 },
                    { 0x9e, 0x2d },
                    { 0xaa, 0x00 },
                },
                {},
            },
            {   { 2400 }, 45000, { ScanMethod::TRANSPARENCY,
                                   ScanMethod::TRANSPARENCY_INFRARED }, {
                    { 0x0c, 0x00 },
                    { 0x16, 0x13 }, { 0x17, 0x15 }, { 0x18, 0x10 }, { 0x19, 0x2a },
                    { 0x1a, 0x30 }, { 0x1b, 0x00 }, { 0x1c, 0x01 }, { 0x1d, 0x75 },
                    { 0x52, 0x0c }, { 0x53, 0x0f }, { 0x54, 0x00 }, { 0x55, 0x03 },
                    { 0x56, 0x06 }, { 0x57, 0x09 }, { 0x58, 0x6b }, { 0x59, 0x00 }, { 0x5a, 0x40 },
                    { 0x70, 0x00 }, { 0x71, 0x02 }, { 0x72, 0x02 }, { 0x73, 0x04 },
                    { 0x74, 0x03 }, { 0x75, 0xfe }, { 0x76, 0x00 },
                    { 0x77, 0x03 }, { 0x78, 0xfe }, { 0x79, 0x00 },
                    { 0x7a, 0x00 }, { 0x7b, 0x92 }, { 0x7c, 0x49 },
                    { 0x9e, 0x2d },
                    { 0xaa, 0x00 },
                },
                {},
            },
            {   { 4800 }, 45000, { ScanMethod::TRANSPARENCY,
                                   ScanMethod::TRANSPARENCY_INFRARED }, {
                    { 0x0c, 0x00 },
                    { 0x16, 0x13 }, { 0x17, 0x15 }, { 0x18, 0x10 }, { 0x19, 0x2a },
                    { 0x1a, 0x30 }, { 0x1b, 0x00 }, { 0x1c, 0x61 }, { 0x1d, 0x75 },
                    { 0x52, 0x03 }, { 0x53, 0x06 }, { 0x54, 0x09 }, { 0x55, 0x0c },
                    { 0x56, 0x0f }, { 0x57, 0x00 }, { 0x58, 0x23 }, { 0x59, 0x00 }, { 0x5a, 0x40 },
                    { 0x70, 0x0a }, { 0x71, 0x0c }, { 0x72, 0x0c }, { 0x73, 0x0e },
                    { 0x74, 0x03 }, { 0x75, 0xff }, { 0x76, 0xff },
                    { 0x77, 0x03 }, { 0x78, 0xff }, { 0x79, 0xff },
                    { 0x7a, 0x00 }, { 0x7b, 0x92 }, { 0x7c, 0x49 },
                    { 0x9e, 0x2d },
                    { 0xaa, 0x00 },
                },
                {   { 0x03, 0x1f },
                },
            },
        };

        for (const CustomSensorSettings& setting : custom_settings) {
            for (auto method : setting.methods) {
                sensor.resolutions = setting.resolutions;
                sensor.method = method;
                sensor.exposure_lperiod = setting.exposure_lperiod;
                sensor.custom_regs = setting.extra_custom_regs;
                sensor.custom_fe_regs = setting.custom_fe_regs;
                s_sensors->push_back(sensor);
            }
        }
    }


    sensor = Genesys_Sensor();
    sensor.sensor_id = SensorId::CCD_HP_N6310;
    sensor.optical_res = 2400;
    // sensor.ccd_size_divisor = 2; Possibly half CCD, needs checking
    sensor.black_pixels = 96;
    sensor.dummy_pixel = 26;
    sensor.ccd_start_xoffset = 128;
    sensor.fau_gain_white_ref = 210;
    sensor.gain_white_ref = 230;
    sensor.exposure = { 0x0000, 0x0000, 0x0000 };
    sensor.custom_regs = {
        { 0x16, 0x33 },
        { 0x17, 0x0c },
        { 0x18, 0x02 },
        { 0x19, 0x2a },
        { 0x1a, 0x30 },
        { 0x1b, 0x00 },
        { 0x1c, 0x00 },
        { 0x1d, 0x08 },
        { 0x52, 0x0b },
        { 0x53, 0x0e },
        { 0x54, 0x11 },
        { 0x55, 0x02 },
        { 0x56, 0x05 },
        { 0x57, 0x08 },
        { 0x58, 0x63 },
        { 0x59, 0x00 },
        { 0x5a, 0x40 },
    };
    sensor.gamma = { 1.0f, 1.0f, 1.0f };
    sensor.get_register_hwdpi_fun = default_get_register_hwdpi;
    sensor.get_hwdpi_divisor_fun = default_get_hwdpi_divisor_for_dpi;
    sensor.get_ccd_size_divisor_fun = default_get_ccd_size_divisor_for_dpi;
    s_sensors->push_back(sensor);


    sensor = Genesys_Sensor();
    sensor.sensor_id = SensorId::CIS_CANON_LIDE_110;
    sensor.optical_res = 2400;
    sensor.ccd_size_divisor = 2;
    sensor.black_pixels = 87;
    sensor.dummy_pixel = 16;
    sensor.ccd_start_xoffset = 303;
    sensor.fau_gain_white_ref = 210;
    sensor.gain_white_ref = 200;
    sensor.exposure = { 0x0000, 0x0000, 0x0000 };
    sensor.gamma = { 2.2f, 2.2f, 2.2f };
    sensor.get_register_hwdpi_fun = default_get_register_hwdpi;
    sensor.get_hwdpi_divisor_fun = default_get_hwdpi_divisor_for_dpi;
    sensor.get_ccd_size_divisor_fun = get_ccd_size_divisor_gl124;

    {
        struct CustomSensorSettings {
            ResolutionFilter resolutions;
            int exposure_lperiod;
            SensorExposure exposure;
            std::vector<unsigned> segment_order;
            GenesysRegisterSettingSet custom_regs;
        };

        CustomSensorSettings custom_settings[] = {
            {   { 75, 100, 150 }, 4608, { 462, 609, 453 }, std::vector<unsigned>{}, {
                    { 0x16, 0x10 }, { 0x17, 0x04 }, { 0x18, 0x00 }, { 0x19, 0x01 },
                    { 0x1a, 0x30 }, { 0x1b, 0x00 }, { 0x1c, 0x02 }, { 0x1d, 0x01 }, { 0x20, 0x0c },
                    { 0x52, 0x00 }, { 0x53, 0x02 }, { 0x54, 0x04 }, { 0x55, 0x06 },
                    { 0x56, 0x04 }, { 0x57, 0x04 }, { 0x58, 0x04 }, { 0x59, 0x04 },
                    { 0x5a, 0x1a }, { 0x5b, 0x00 }, { 0x5c, 0xc0 },
                    { 0x61, 0x20 },
                    { 0x70, 0x06 }, { 0x71, 0x08 }, { 0x72, 0x08 }, { 0x73, 0x0a },
                    { 0x74, 0x00 }, { 0x75, 0x00 }, { 0x76, 0x1e },
                    { 0x77, 0x00 }, { 0x78, 0x00 }, { 0x79, 0x9f },
                    { 0x7a, 0x00 }, { 0x7b, 0x00 }, { 0x7c, 0x55 },
                    { 0x88, 0x00 }, { 0x89, 0x65 },
                    { 0x93, 0x00 }, { 0x94, 0x0a }, { 0x95, 0x18 },
                    { 0x96, 0x00 }, { 0x97, 0x9a },
                    { 0x98, 0x21 },
                }
            },
            {   { 300 }, 4608, { 462, 609, 453 }, std::vector<unsigned>{}, {
                    { 0x16, 0x10 }, { 0x17, 0x04 }, { 0x18, 0x00 }, { 0x19, 0x01 },
                    { 0x1a, 0x30 }, { 0x1b, 0x00 }, { 0x1c, 0x02 }, { 0x1d, 0x00 }, { 0x20, 0x0c },
                    { 0x52, 0x00 }, { 0x53, 0x02 }, { 0x54, 0x04 }, { 0x55, 0x06 },
                    { 0x56, 0x04 }, { 0x57, 0x04 }, { 0x58, 0x04 }, { 0x59, 0x04 },
                    { 0x5a, 0x1a }, { 0x5b, 0x00 }, { 0x5c, 0xc0 },
                    { 0x61, 0x20 },
                    { 0x70, 0x06 }, { 0x71, 0x08 }, { 0x72, 0x08 }, { 0x73, 0x0a },
                    { 0x74, 0x00 }, { 0x75, 0x00 }, { 0x76, 0x1e },
                    { 0x77, 0x00 }, { 0x78, 0x00 }, { 0x79, 0x9f },
                    { 0x7a, 0x00 }, { 0x7b, 0x00 }, { 0x7c, 0x55 },
                    { 0x88, 0x00 }, { 0x89, 0x65 },
                    { 0x93, 0x00 }, { 0x94, 0x0a }, { 0x95, 0x18 },
                    { 0x96, 0x00 }, { 0x97, 0x9a },
                    { 0x98, 0x21 },
                }
            },
            {   { 600 }, 5360, { 823, 1117, 805 }, std::vector<unsigned>{}, {
                    { 0x16, 0x10 }, { 0x17, 0x04 }, { 0x18, 0x00 }, { 0x19, 0x01 },
                    { 0x1a, 0x30 }, { 0x1b, 0x00 }, { 0x1c, 0x02 }, { 0x1d, 0x00 }, { 0x20, 0x0a },
                    { 0x52, 0x00 }, { 0x53, 0x02 }, { 0x54, 0x04 }, { 0x55, 0x06 },
                    { 0x56, 0x04 }, { 0x57, 0x04 }, { 0x58, 0x04 }, { 0x59, 0x04 },
                    { 0x5a, 0x1a }, { 0x5b, 0x00 }, { 0x5c, 0xc0 },
                    { 0x61, 0x20 },
                    { 0x70, 0x06 }, { 0x71, 0x08 }, { 0x72, 0x08 }, { 0x73, 0x0a },
                    { 0x74, 0x00 }, { 0x75, 0x00 }, { 0x76, 0x1e },
                    { 0x77, 0x00 }, { 0x78, 0x00 }, { 0x79, 0x9f },
                    { 0x7a, 0x00 }, { 0x7b, 0x00 }, { 0x7c, 0x55 },
                    { 0x88, 0x00 }, { 0x89, 0x65 },
                    { 0x93, 0x00 }, { 0x94, 0x14 }, { 0x95, 0x30 },
                    { 0x96, 0x00 }, { 0x97, 0xa3 },
                    { 0x98, 0x21 },
                },
            },
            {   { 1200 }, 10528, { 6071, 6670, 6042 }, { 0, 1 }, {
                    { 0x16, 0x10 }, { 0x17, 0x04 }, { 0x18, 0x00 }, { 0x19, 0x01 },
                    { 0x1a, 0x30 }, { 0x1b, 0x00 }, { 0x1c, 0x02 }, { 0x1d, 0x00 },{ 0x20, 0x08 },
                    { 0x52, 0x00 }, { 0x53, 0x02 }, { 0x54, 0x04 }, { 0x55, 0x06 },
                    { 0x56, 0x04 }, { 0x57, 0x04 }, { 0x58, 0x04 }, { 0x59, 0x04 },
                    { 0x5a, 0x1a }, { 0x5b, 0x00 }, { 0x5c, 0xc0 },
                    { 0x61, 0x20 },
                    { 0x70, 0x06 }, { 0x71, 0x08 }, { 0x72, 0x08 }, { 0x73, 0x0a },
                    { 0x74, 0x00 }, { 0x75, 0x00 }, { 0x76, 0x1e },
                    { 0x77, 0x00 }, { 0x78, 0x00 }, { 0x79, 0x9f },
                    { 0x7a, 0x00 }, { 0x7b, 0x00 }, { 0x7c, 0x55 },
                    { 0x88, 0x12 }, { 0x89, 0x47 },
                    { 0x93, 0x00 }, { 0x94, 0x14 }, { 0x95, 0x30 },
                    { 0x96, 0x00 }, { 0x97, 0xa3 },
                    { 0x98, 0x22 },
                }
            },
            {   { 2400 }, 20864, { 7451, 8661, 7405 }, { 0, 2, 1, 3 }, {
                    { 0x16, 0x10 }, { 0x17, 0x04 }, { 0x18, 0x00 }, { 0x19, 0x01 },
                    { 0x1a, 0x30 }, { 0x1b, 0x00 }, { 0x1c, 0x02 }, { 0x1d, 0x00 }, { 0x20, 0x06 },
                    { 0x52, 0x00 }, { 0x53, 0x02 }, { 0x54, 0x04 }, { 0x55, 0x06 },
                    { 0x56, 0x04 }, { 0x57, 0x04 }, { 0x58, 0x04 }, { 0x59, 0x04 },
                    { 0x5a, 0x1a }, { 0x5b, 0x00 }, { 0x5c, 0xc0 },
                    { 0x61, 0x20 },
                    { 0x70, 0x06 }, { 0x71, 0x08 }, { 0x72, 0x08 }, { 0x73, 0x0a },
                    { 0x74, 0x00 }, { 0x75, 0x00 }, { 0x76, 0x1e },
                    { 0x77, 0x00 }, { 0x78, 0x00 }, { 0x79, 0x9f },
                    { 0x7a, 0x00 }, { 0x7b, 0x00 }, { 0x7c, 0x55 },
                    { 0x88, 0x12 }, { 0x89, 0x47 },
                    { 0x93, 0x00 }, { 0x94, 0x14 }, { 0x95, 0x30 },
                    { 0x96, 0x00 }, { 0x97, 0xa3 },
                    { 0x98, 0x24 },
                }
            }
        };

        for (const auto& setting : custom_settings) {
            sensor.resolutions = setting.resolutions;
            sensor.exposure_lperiod = setting.exposure_lperiod;
            sensor.exposure = setting.exposure;
            sensor.segment_order = setting.segment_order;
            sensor.custom_regs = setting.custom_regs;
            s_sensors->push_back(sensor);
        }
    }

    sensor = Genesys_Sensor();
    sensor.sensor_id = SensorId::CIS_CANON_LIDE_120;
    sensor.optical_res = 2400;
    sensor.ccd_size_divisor = 2;
    sensor.black_pixels = 87;
    sensor.dummy_pixel = 16;
    sensor.ccd_start_xoffset = 303;
    sensor.fau_gain_white_ref = 210;
    sensor.gain_white_ref = 200;
    sensor.exposure = { 0x0000, 0x0000, 0x0000 };
    sensor.gamma = { 2.2f, 2.2f, 2.2f };
    sensor.get_register_hwdpi_fun = default_get_register_hwdpi;
    sensor.get_hwdpi_divisor_fun = default_get_hwdpi_divisor_for_dpi;
    sensor.get_ccd_size_divisor_fun = get_ccd_size_divisor_gl124;

    {
        struct CustomSensorSettings {
            ResolutionFilter resolutions;
            int exposure_lperiod;
            SensorExposure exposure;
            std::vector<unsigned> segment_order;
            GenesysRegisterSettingSet custom_regs;
        };

        CustomSensorSettings custom_settings[] = {
            {   { 75, 100, 150, 300 }, 4608, { 1244, 1294, 1144 }, std::vector<unsigned>{}, {
                    { 0x16, 0x15 }, { 0x17, 0x04 }, { 0x18, 0x00 }, { 0x19, 0x01 },
                    { 0x1a, 0x30 }, { 0x1b, 0x00 }, { 0x1c, 0x02 }, { 0x1d, 0x00 }, { 0x20, 0x02 },
                    { 0x52, 0x04 }, { 0x53, 0x06 }, { 0x54, 0x00 }, { 0x55, 0x02 },
                    { 0x56, 0x04 }, { 0x57, 0x04 }, { 0x58, 0x04 }, { 0x59, 0x04 },
                    { 0x5a, 0x3a }, { 0x5b, 0x00 }, { 0x5c, 0x00 },
                    { 0x61, 0x20 },
                    { 0x70, 0x00 }, { 0x71, 0x1f }, { 0x72, 0x08 }, { 0x73, 0x0a },
                    { 0x74, 0x00 }, { 0x75, 0x00 }, { 0x76, 0x0f },
                    { 0x77, 0x00 }, { 0x78, 0x00 }, { 0x79, 0x00 },
                    { 0x7a, 0x00 }, { 0x7b, 0x00 }, { 0x7c, 0x55 },
                    { 0x88, 0x00 }, { 0x89, 0x5e },
                    { 0x93, 0x00 }, { 0x94, 0x09 }, { 0x95, 0xf8 },
                    { 0x96, 0x00 }, { 0x97, 0x70 },
                    { 0x98, 0x21 },
                },
            },
            {   { 600 }, 5360, { 2394, 2444, 2144 }, std::vector<unsigned>{}, {
                    { 0x16, 0x11 }, { 0x17, 0x04 }, { 0x18, 0x00 }, { 0x19, 0x01 },
                    { 0x1a, 0x30 }, { 0x1b, 0x00 }, { 0x1c, 0x02 }, { 0x1d, 0x00 }, { 0x20, 0x02 },
                    { 0x52, 0x04 }, { 0x53, 0x06 }, { 0x54, 0x00 }, { 0x55, 0x02 },
                    { 0x56, 0x04 }, { 0x57, 0x04 }, { 0x58, 0x04 }, { 0x59, 0x04 },
                    { 0x5a, 0x3a }, { 0x5b, 0x00 }, { 0x5c, 0x00 },
                    { 0x61, 0x20 },
                    { 0x70, 0x1f }, { 0x71, 0x1f }, { 0x72, 0x08 }, { 0x73, 0x0a },
                    { 0x74, 0x00 }, { 0x75, 0x00 }, { 0x76, 0x0f },
                    { 0x77, 0x00 }, { 0x78, 0x00 }, { 0x79, 0x00 },
                    { 0x7a, 0x00 }, { 0x7b, 0x00 }, { 0x7c, 0x55 },
                    { 0x88, 0x00 }, { 0x89, 0x5e },
                    { 0x93, 0x00 }, { 0x94, 0x13 }, { 0x95, 0xf0 },
                    { 0x96, 0x00 }, { 0x97, 0x8b },
                    { 0x98, 0x21 },
                },
            },
            {   { 1200 }, 10528, { 4694, 4644, 4094 }, std::vector<unsigned>{}, {
                    { 0x16, 0x15 }, { 0x17, 0x04 }, { 0x18, 0x00 }, { 0x19, 0x01 },
                    { 0x1a, 0x30 }, { 0x1b, 0x00 }, { 0x1c, 0x02 }, { 0x1d, 0x00 }, { 0x20, 0x02 },
                    { 0x52, 0x04 }, { 0x53, 0x06 }, { 0x54, 0x00 }, { 0x55, 0x02 },
                    { 0x56, 0x04 }, { 0x57, 0x04 }, { 0x58, 0x04 }, { 0x59, 0x04 },
                    { 0x5a, 0x3a }, { 0x5b, 0x00 }, { 0x5c, 0x00 },
                    { 0x61, 0x20 },
                    { 0x70, 0x1f }, { 0x71, 0x1f }, { 0x72, 0x08 }, { 0x73, 0x0a },
                    { 0x74, 0x00 }, { 0x75, 0x00 }, { 0x76, 0x0f },
                    { 0x77, 0x00 }, { 0x78, 0x00 }, { 0x79, 0x00 },
                    { 0x7a, 0x00 }, { 0x7b, 0x00 }, { 0x7c, 0x55 },
                    { 0x88, 0x00 }, { 0x89, 0x5e },
                    { 0x93, 0x00 }, { 0x94, 0x27 }, { 0x95, 0xe0 },
                    { 0x96, 0x00 }, { 0x97, 0xc0 },
                    { 0x98, 0x21 },
                },
            },
            {   { 2400 }, 20864, { 8944, 8144, 7994 }, std::vector<unsigned>{}, {
                    { 0x16, 0x11 }, { 0x17, 0x04 }, { 0x18, 0x00 }, { 0x19, 0x01 },
                    { 0x1a, 0x30 }, { 0x1b, 0x00 }, { 0x1c, 0x02 }, { 0x1d, 0x00 }, { 0x20, 0x02 },
                    { 0x52, 0x04 }, { 0x53, 0x06 }, { 0x54, 0x00 }, { 0x55, 0x02 },
                    { 0x56, 0x04 }, { 0x57, 0x04 }, { 0x58, 0x04 }, { 0x59, 0x04 },
                    { 0x5a, 0x3a }, { 0x5b, 0x00 }, { 0x5c, 0x00 },
                    { 0x61, 0x20 },
                    { 0x70, 0x00 }, { 0x71, 0x1f }, { 0x72, 0x08 }, { 0x73, 0x0a },
                    { 0x74, 0x00 }, { 0x75, 0x00 }, { 0x76, 0x0f },
                    { 0x77, 0x00 }, { 0x78, 0x00 }, { 0x79, 0x00 },
                    { 0x7a, 0x00 }, { 0x7b, 0x00 }, { 0x7c, 0x55 },
                    { 0x88, 0x00 }, { 0x89, 0x5e },
                    { 0x93, 0x00 }, { 0x94, 0x4f }, { 0x95, 0xc0 },
                    { 0x96, 0x01 }, { 0x97, 0x2a },
                    { 0x98, 0x21 },
                }
            },
        };

        for (const auto& setting : custom_settings) {
            sensor.resolutions = setting.resolutions;
            sensor.exposure_lperiod = setting.exposure_lperiod;
            sensor.exposure = setting.exposure;
            sensor.segment_order = setting.segment_order;
            sensor.custom_regs = setting.custom_regs;
            s_sensors->push_back(sensor);
        }
    }

    sensor = Genesys_Sensor();
    sensor.sensor_id = SensorId::CIS_CANON_LIDE_210;
    sensor.optical_res = 2400;
    sensor.ccd_size_divisor = 2;
    sensor.black_pixels = 87;
    sensor.dummy_pixel = 16;
    sensor.ccd_start_xoffset = 303;
    sensor.fau_gain_white_ref = 210;
    sensor.gain_white_ref = 200;
    sensor.exposure = { 0x0000, 0x0000, 0x0000 };
    sensor.gamma = { 2.2f, 2.2f, 2.2f };
    sensor.get_register_hwdpi_fun = default_get_register_hwdpi;
    sensor.get_hwdpi_divisor_fun = default_get_hwdpi_divisor_for_dpi;
    sensor.get_ccd_size_divisor_fun = get_ccd_size_divisor_gl124;

    {
        struct CustomSensorSettings {
            ResolutionFilter resolutions;
            int exposure_lperiod;
            SensorExposure exposure;
            std::vector<unsigned> segment_order;
            GenesysRegisterSettingSet custom_regs;
        };

        CustomSensorSettings custom_settings[] = {
            {   { 75, 100, 150, 300 }, 2768, { 388, 574, 393 }, std::vector<unsigned>{}, {
                    // { 0x16, 0x00 }, // FIXME: check if default value is different
                    { 0x16, 0x10 }, { 0x17, 0x04 }, { 0x18, 0x00 }, { 0x19, 0x01 },
                    { 0x1a, 0x30 }, { 0x1b, 0x00 }, { 0x1c, 0x02 }, { 0x1d, 0x01 }, { 0x20, 0x0c },
                    { 0x52, 0x00 }, { 0x53, 0x02 }, { 0x54, 0x04 }, { 0x55, 0x06 },
                    { 0x56, 0x04 }, { 0x57, 0x04 }, { 0x58, 0x04 }, { 0x59, 0x04 },
                    { 0x5a, 0x1a }, { 0x5b, 0x00 }, { 0x5c, 0xc0 },
                    { 0x61, 0x20 },
                    // { 0x70, 0x00 }, // FIXME: check if default value is different
                    { 0x74, 0x00 }, { 0x75, 0x00 }, { 0x76, 0x1e },
                    { 0x77, 0x00 }, { 0x78, 0x00 }, { 0x79, 0x9f },
                    { 0x7a, 0x00 }, { 0x7b, 0x00 }, { 0x7c, 0x55 },
                    { 0x88, 0x00 }, { 0x89, 0x65 },
                    { 0x93, 0x00 }, { 0x94, 0x0a }, { 0x95, 0x18 },
                    { 0x96, 0x00 }, { 0x97, 0x9a },
                    { 0x98, 0x21 },
                }
            },
            {   { 600 }, 5360, { 388, 574, 393 }, std::vector<unsigned>{}, {
                    // { 0x16, 0x00 }, // FIXME: check if default value is different
                    { 0x16, 0x10 }, { 0x17, 0x04 }, { 0x18, 0x00 }, { 0x19, 0x01 },
                    { 0x1a, 0x30 }, { 0x1b, 0x00 }, { 0x1c, 0x02 }, { 0x1d, 0x01 }, { 0x20, 0x0a },
                    { 0x52, 0x00 }, { 0x53, 0x02 }, { 0x54, 0x04 }, { 0x55, 0x06 },
                    { 0x56, 0x04 }, { 0x57, 0x04 }, { 0x58, 0x04 }, { 0x59, 0x04 },
                    { 0x5a, 0x1a }, { 0x5b, 0x00 }, { 0x5c, 0xc0 },
                    { 0x61, 0x20 },
                    // { 0x70, 0x00 }, // FIXME: check if default value is different
                    { 0x74, 0x00 }, { 0x75, 0x00 }, { 0x76, 0x1e },
                    { 0x77, 0x00 }, { 0x78, 0x00 }, { 0x79, 0x9f },
                    { 0x7a, 0x00 }, { 0x7b, 0x00 }, { 0x7c, 0x55 },
                    { 0x88, 0x00 }, { 0x89, 0x65 },
                    { 0x93, 0x00 }, { 0x94, 0x14 }, { 0x95, 0x30 },
                    { 0x96, 0x00 }, { 0x97, 0xa3 },
                    { 0x98, 0x21 },
                }
            },
            {   { 1200 }, 10528, { 388, 574, 393 }, {0, 1}, {
                    // { 0x16, 0x00 }, // FIXME: check if default value is different
                    { 0x16, 0x10 }, { 0x17, 0x04 }, { 0x18, 0x00 }, { 0x19, 0x01 },
                    { 0x1a, 0x30 }, { 0x1b, 0x00 }, { 0x1c, 0x02 }, { 0x1d, 0x01 }, { 0x20, 0x08 },
                    { 0x52, 0x00 }, { 0x53, 0x02 }, { 0x54, 0x04 }, { 0x55, 0x06 },
                    { 0x56, 0x04 }, { 0x57, 0x04 }, { 0x58, 0x04 }, { 0x59, 0x04 },
                    { 0x5a, 0x1a }, { 0x5b, 0x00 }, { 0x5c, 0xc0 },
                    { 0x61, 0x20 },
                    // { 0x70, 0x00 }, // FIXME: check if default value is different
                    { 0x74, 0x00 }, { 0x75, 0x00 }, { 0x76, 0x1e },
                    { 0x77, 0x00 }, { 0x78, 0x00 }, { 0x79, 0x9f },
                    { 0x7a, 0x00 }, { 0x7b, 0x00 }, { 0x7c, 0x55 },
                    { 0x88, 0x00 }, { 0x89, 0x65 },
                    { 0x93, 0x00 }, { 0x94, 0x14 }, { 0x95, 0x30 },
                    { 0x96, 0x00 }, { 0x97, 0xa3 },
                    { 0x98, 0x22 },
                },
            },
            {   { 2400 }, 20864, { 6839, 8401, 6859 }, {0, 2, 1, 3}, {
                    // { 0x16, 0x00 }, // FIXME: check if default value is different
                    { 0x16, 0x10 }, { 0x17, 0x04 }, { 0x18, 0x00 }, { 0x19, 0x01 },
                    { 0x1a, 0x30 }, { 0x1b, 0x00 }, { 0x1c, 0x02 }, { 0x1d, 0x01 }, { 0x20, 0x06 },
                    { 0x52, 0x00 }, { 0x53, 0x02 }, { 0x54, 0x04 }, { 0x55, 0x06 },
                    { 0x56, 0x04 }, { 0x57, 0x04 }, { 0x58, 0x04 }, { 0x59, 0x04 },
                    { 0x5a, 0x1a }, { 0x5b, 0x00 }, { 0x5c, 0xc0 },
                    { 0x61, 0x20 },
                    // { 0x70, 0x00 }, // FIXME: check if default value is different
                    { 0x74, 0x00 }, { 0x75, 0x00 }, { 0x76, 0x1e },
                    { 0x77, 0x00 }, { 0x78, 0x00 }, { 0x79, 0x9f },
                    { 0x7a, 0x00 }, { 0x7b, 0x00 }, { 0x7c, 0x55 },
                    { 0x88, 0x12 }, { 0x89, 0x47 },
                    { 0x93, 0x00 }, { 0x94, 0x14 }, { 0x95, 0x30 },
                    { 0x96, 0x00 }, { 0x97, 0xa3 },
                    { 0x98, 0x24 },
                },
            }
        };

        for (const auto& setting : custom_settings) {
            sensor.resolutions = setting.resolutions;
            sensor.exposure_lperiod = setting.exposure_lperiod;
            sensor.exposure = setting.exposure;
            sensor.segment_order = setting.segment_order;
            sensor.custom_regs = setting.custom_regs;
            s_sensors->push_back(sensor);
        }
    }

    sensor = Genesys_Sensor();
    sensor.sensor_id = SensorId::CIS_CANON_LIDE_220;
    sensor.optical_res = 2400;
    sensor.ccd_size_divisor = 2;
    sensor.black_pixels = 87;
    sensor.dummy_pixel = 16;
    sensor.ccd_start_xoffset = 303;
    sensor.fau_gain_white_ref = 210;
    sensor.gain_white_ref = 200;
    sensor.exposure = { 0x0000, 0x0000, 0x0000 };
    sensor.gamma = { 2.2f, 2.2f, 2.2f };
    sensor.get_register_hwdpi_fun = default_get_register_hwdpi;
    sensor.get_hwdpi_divisor_fun = default_get_hwdpi_divisor_for_dpi;
    sensor.get_ccd_size_divisor_fun = get_ccd_size_divisor_gl124;

    {
        struct CustomSensorSettings {
            ResolutionFilter resolutions;
            int exposure_lperiod;
            SensorExposure exposure;
            std::vector<unsigned> segment_order;
            GenesysRegisterSettingSet custom_regs;
        };

        CustomSensorSettings custom_settings[] = {
            {   { 75, 100, 150, 300 }, 2768, { 388, 574, 393 }, std::vector<unsigned>{}, {
                    // { 0x16, 0x00 }, // FIXME: check if default value is different
                    { 0x16, 0x10 }, { 0x17, 0x04 }, { 0x18, 0x00 }, { 0x19, 0x01 },
                    { 0x1a, 0x30 }, { 0x1b, 0x00 }, { 0x1c, 0x02 }, { 0x1d, 0x01 }, { 0x20, 0x0c },
                    { 0x52, 0x00 }, { 0x53, 0x02 }, { 0x54, 0x04 }, { 0x55, 0x06 },
                    { 0x56, 0x04 }, { 0x57, 0x04 }, { 0x58, 0x04 }, { 0x59, 0x04 },
                    { 0x5a, 0x1a }, { 0x5b, 0x00 }, { 0x5c, 0xc0 },
                    { 0x61, 0x20 },
                    // { 0x70, 0x00 }, // FIXME: check if default value is different
                    { 0x74, 0x00 }, { 0x75, 0x00 }, { 0x76, 0x0f },
                    { 0x77, 0x00 }, { 0x78, 0x00 }, { 0x79, 0x9f },
                    { 0x7a, 0x00 }, { 0x7b, 0x00 }, { 0x7c, 0x55 },
                    { 0x88, 0x00 }, { 0x89, 0x65 },
                    { 0x93, 0x00 }, { 0x94, 0x0a }, { 0x95, 0x18 },
                    { 0x96, 0x00 }, { 0x97, 0x9a },
                    { 0x98, 0x21 },
                }
            },
            {   { 600 }, 5360, { 388, 574, 393 }, std::vector<unsigned>{}, {
                    // { 0x16, 0x00 }, // FIXME: check if default value is different
                    { 0x16, 0x10 }, { 0x17, 0x04 }, { 0x18, 0x00 }, { 0x19, 0x01 },
                    { 0x1a, 0x30 }, { 0x1b, 0x00 }, { 0x1c, 0x02 }, { 0x1d, 0x01 }, { 0x20, 0x0a },
                    { 0x52, 0x00 }, { 0x53, 0x02 }, { 0x54, 0x04 }, { 0x55, 0x06 },
                    { 0x56, 0x04 }, { 0x57, 0x04 }, { 0x58, 0x04 }, { 0x59, 0x04 },
                    { 0x5a, 0x1a }, { 0x5b, 0x00 }, { 0x5c, 0xc0 },
                    { 0x61, 0x20 },
                    // { 0x70, 0x00 }, // FIXME: check if default value is different
                    { 0x74, 0x00 }, { 0x75, 0x00 }, { 0x76, 0x0f },
                    { 0x77, 0x00 }, { 0x78, 0x00 }, { 0x79, 0x9f },
                    { 0x7a, 0x00 }, { 0x7b, 0x00 }, { 0x7c, 0x55 },
                    { 0x88, 0x00 }, { 0x89, 0x65 },
                    { 0x93, 0x00 }, { 0x94, 0x14 }, { 0x95, 0x30 },
                    { 0x96, 0x00 }, { 0x97, 0xa3 },
                    { 0x98, 0x21 },
                }
            },
            {   { 1200 }, 10528, { 388, 574, 393 }, {0, 1}, {
                    // { 0x16, 0x00 }, // FIXME: check if default value is different
                    { 0x16, 0x10 }, { 0x17, 0x04 }, { 0x18, 0x00 }, { 0x19, 0x01 },
                    { 0x1a, 0x30 }, { 0x1b, 0x00 }, { 0x1c, 0x02 }, { 0x1d, 0x01 }, { 0x20, 0x08 },
                    { 0x52, 0x00 }, { 0x53, 0x02 }, { 0x54, 0x04 }, { 0x55, 0x06 },
                    { 0x56, 0x04 }, { 0x57, 0x04 }, { 0x58, 0x04 }, { 0x59, 0x04 },
                    { 0x5a, 0x1a }, { 0x5b, 0x00 }, { 0x5c, 0xc0 },
                    { 0x61, 0x20 },
                    // { 0x70, 0x00 }, // FIXME: check if default value is different
                    { 0x74, 0x00 }, { 0x75, 0x00 }, { 0x76, 0x0f },
                    { 0x77, 0x00 }, { 0x78, 0x00 }, { 0x79, 0x9f },
                    { 0x7a, 0x00 }, { 0x7b, 0x00 }, { 0x7c, 0x55 },
                    { 0x88, 0x00 }, { 0x89, 0x65 },
                    { 0x93, 0x00 }, { 0x94, 0x14 }, { 0x95, 0x30 },
                    { 0x96, 0x00 }, { 0x97, 0xa3 },
                    { 0x98, 0x22 },
                }
            },
            {   { 2400 }, 20864, { 6839, 8401, 6859 }, {0, 2, 1, 3}, {
                    // { 0x16, 0x00 }, // FIXME: check if default value is different
                    { 0x16, 0x10 }, { 0x17, 0x04 }, { 0x18, 0x00 }, { 0x19, 0x01 },
                    { 0x1a, 0x30 }, { 0x1b, 0x00 }, { 0x1c, 0x02 }, { 0x1d, 0x01 }, { 0x20, 0x06 },
                    { 0x52, 0x00 }, { 0x53, 0x02 }, { 0x54, 0x04 }, { 0x55, 0x06 },
                    { 0x56, 0x04 }, { 0x57, 0x04 }, { 0x58, 0x04 }, { 0x59, 0x04 },
                    { 0x5a, 0x1a }, { 0x5b, 0x00 }, { 0x5c, 0xc0 },
                    { 0x61, 0x20 },
                    // { 0x70, 0x00 }, // FIXME: check if default value is different
                    { 0x74, 0x00 }, { 0x75, 0x00 }, { 0x76, 0x0f },
                    { 0x77, 0x00 }, { 0x78, 0x00 }, { 0x79, 0x9f },
                    { 0x7a, 0x00 }, { 0x7b, 0x00 }, { 0x7c, 0x55 },
                    { 0x88, 0x12 }, { 0x89, 0x47 },
                    { 0x93, 0x00 }, { 0x94, 0x14 }, { 0x95, 0x30 },
                    { 0x96, 0x00 }, { 0x97, 0xa3 },
                    { 0x98, 0x24 },
                },
            }
        };

        for (const auto& setting : custom_settings) {
            sensor.resolutions = setting.resolutions;
            sensor.exposure_lperiod = setting.exposure_lperiod;
            sensor.exposure = setting.exposure;
            sensor.segment_order = setting.segment_order;
            sensor.custom_regs = setting.custom_regs;
            s_sensors->push_back(sensor);
        }
    }

    sensor = Genesys_Sensor();
    sensor.sensor_id = SensorId::CCD_PLUSTEK_OPTICPRO_3600;
    sensor.optical_res = 1200;
    sensor.ccd_size_divisor = 2;
    sensor.black_pixels = 87;
    sensor.dummy_pixel = 87;
    sensor.ccd_start_xoffset = 0;
    sensor.fau_gain_white_ref = 210;
    sensor.gain_white_ref = 230;
    sensor.exposure = { 0x0000, 0x0000, 0x0000 };
    sensor.custom_regs = {
        { 0x08, 0x00 },
        { 0x09, 0x00 },
        { 0x0a, 0x00 },
        { 0x0b, 0x00 },
        { 0x16, 0x33 },
        { 0x17, 0x0b },
        { 0x18, 0x11 },
        { 0x19, 0x2a },
        { 0x1a, 0x00 },
        { 0x1b, 0x00 },
        { 0x1c, 0x00 },
        { 0x1d, 0xc4 },
        { 0x52, 0x07 }, // [GB](HI|LOW) not needed for cis
        { 0x53, 0x0a },
        { 0x54, 0x0c },
        { 0x55, 0x00 },
        { 0x56, 0x02 },
        { 0x57, 0x06 },
        { 0x58, 0x22 },
        { 0x59, 0x69 },
        { 0x5a, 0x40 },
        { 0x5b, 0x00 }, // TODO: 5b-5e
        { 0x5c, 0x00 },
        { 0x5d, 0x00 },
        { 0x5e, 0x02 },
    };
    sensor.gamma = { 1.0f, 1.0f, 1.0f };
    sensor.get_register_hwdpi_fun = default_get_register_hwdpi;
    sensor.get_hwdpi_divisor_fun = default_get_hwdpi_divisor_for_dpi;
    sensor.get_ccd_size_divisor_fun = default_get_ccd_size_divisor_for_dpi;
    s_sensors->push_back(sensor);


    sensor = Genesys_Sensor();
    sensor.sensor_id = SensorId::CCD_PLUSTEK_OPTICFILM_7200I;
    sensor.optical_res = 7200;
    sensor.register_dpihw_override = 1200;
    sensor.black_pixels = 88; // TODO
    sensor.dummy_pixel = 20;
    sensor.ccd_start_xoffset = 0;
    sensor.fau_gain_white_ref = 210;
    sensor.gain_white_ref = 230;
    sensor.exposure = { 0x0000, 0x0000, 0x0000 };
    sensor.stagger_config = StaggerConfig{7200, 4};
    sensor.use_host_side_calib = true;
    sensor.custom_regs = {
        { 0x08, 0x00 },
        { 0x09, 0x00 },
        { 0x0a, 0x00 },
        { 0x16, 0x23 },
        { 0x17, 0x0c },
        { 0x18, 0x10 },
        { 0x19, 0x2a },
        { 0x1a, 0x00 },
        { 0x1b, 0x00 },
        { 0x1c, 0x21 },
        { 0x1d, 0x84 },
        { 0x52, 0x0a },
        { 0x53, 0x0d },
        { 0x54, 0x10 },
        { 0x55, 0x01 },
        { 0x56, 0x04 },
        { 0x57, 0x07 },
        { 0x58, 0x3a },
        { 0x59, 0x81 },
        { 0x5a, 0xc0 },
        { 0x70, 0x0a },
        { 0x71, 0x0b },
        { 0x72, 0x0c },
        { 0x73, 0x0d },
        { 0x74, 0x00 }, { 0x75, 0x00 }, { 0x76, 0x00 },
        { 0x77, 0x00 }, { 0x78, 0x00 }, { 0x79, 0x00 },
        { 0x7a, 0x00 }, { 0x7b, 0x00 }, { 0x7c, 0x00 },
    };
    sensor.gamma = { 1.0f, 1.0f, 1.0f };
    sensor.get_register_hwdpi_fun = default_get_register_hwdpi;
    sensor.get_hwdpi_divisor_fun = default_get_hwdpi_divisor_for_dpi;
    sensor.get_ccd_size_divisor_fun = get_ccd_size_divisor_exact;
    {
        struct CustomSensorSettings
        {
            ResolutionFilter resolutions;
            ScanMethod method;
            unsigned ccd_size_divisor;
            unsigned logical_dpihw_override;
            unsigned pixel_count_multiplier;
            unsigned exposure_lperiod;
            unsigned dpiset_override;
            GenesysRegisterSettingSet custom_fe_regs;
        };

        CustomSensorSettings custom_settings[] = {
            { { 900 }, ScanMethod::TRANSPARENCY, 1, 900, 8, 0x2538, 150, {} },
            { { 1800 }, ScanMethod::TRANSPARENCY, 1, 1800, 4, 0x2538, 300, {} },
            { { 3600 }, ScanMethod::TRANSPARENCY, 1, 3600, 2, 0x2538, 600, {} },
            { { 7200 }, ScanMethod::TRANSPARENCY, 1, 7200, 1, 0x19c8, 1200, {
                    { 0x02, 0x1b },
                    { 0x03, 0x14 },
                    { 0x04, 0x20 },
                }
            },
            { { 900 }, ScanMethod::TRANSPARENCY_INFRARED, 1, 900, 8, 0x1f54, 150, {} },
            { { 1800 }, ScanMethod::TRANSPARENCY_INFRARED, 1, 1800, 4, 0x1f54, 300, {} },
            { { 3600 }, ScanMethod::TRANSPARENCY_INFRARED, 1, 3600, 2, 0x1f54, 600, {} },
            { { 7200 }, ScanMethod::TRANSPARENCY_INFRARED, 1, 7200, 1, 0x1f54, 1200, {} },
        };

        for (const CustomSensorSettings& setting : custom_settings) {
            sensor.resolutions = setting.resolutions;
            sensor.method = setting.method;
            sensor.ccd_size_divisor = setting.ccd_size_divisor;
            sensor.logical_dpihw_override = setting.logical_dpihw_override;
            sensor.pixel_count_multiplier = setting.pixel_count_multiplier;
            sensor.exposure_lperiod = setting.exposure_lperiod;
            sensor.dpiset_override = setting.dpiset_override;
            sensor.custom_fe_regs = setting.custom_fe_regs;
            s_sensors->push_back(sensor);
        }
    }


    sensor = Genesys_Sensor();
    sensor.sensor_id = SensorId::CCD_PLUSTEK_OPTICFILM_7300;
    sensor.optical_res = 7200;
    sensor.method = ScanMethod::TRANSPARENCY;
    sensor.register_dpihw_override = 1200;
    sensor.black_pixels = 88; // TODO
    sensor.dummy_pixel = 20;
    sensor.ccd_start_xoffset = 0;
    sensor.fau_gain_white_ref = 210;
    sensor.gain_white_ref = 230;
    sensor.exposure = { 0x0000, 0x0000, 0x0000 };
    sensor.exposure_lperiod = 0x2f44;
    sensor.stagger_config = StaggerConfig{7200, 4};
    sensor.use_host_side_calib = true;
    sensor.custom_regs = {
        { 0x08, 0x00 },
        { 0x09, 0x00 },
        { 0x0a, 0x00 },
        { 0x16, 0x27 },
        { 0x17, 0x0c },
        { 0x18, 0x10 },
        { 0x19, 0x2a },
        { 0x1a, 0x00 },
        { 0x1b, 0x00 },
        { 0x1c, 0x20 },
        { 0x1d, 0x84 },
        { 0x52, 0x0a },
        { 0x53, 0x0d },
        { 0x54, 0x0f },
        { 0x55, 0x01 },
        { 0x56, 0x04 },
        { 0x57, 0x07 },
        { 0x58, 0x31 },
        { 0x59, 0x79 },
        { 0x5a, 0xc0 },
        { 0x70, 0x0c },
        { 0x71, 0x0d },
        { 0x72, 0x0e },
        { 0x73, 0x0f },
        { 0x74, 0x00 }, { 0x75, 0x00 }, { 0x76, 0x00 },
        { 0x77, 0x00 }, { 0x78, 0x00 }, { 0x79, 0x00 },
        { 0x7a, 0x00 }, { 0x7b, 0x00 }, { 0x7c, 0x00 },
    };
    sensor.gamma = { 1.0f, 1.0f, 1.0f };
    sensor.get_register_hwdpi_fun = default_get_register_hwdpi;
    sensor.get_hwdpi_divisor_fun = default_get_hwdpi_divisor_for_dpi;
    sensor.get_ccd_size_divisor_fun = get_ccd_size_divisor_exact;
    {
        struct CustomSensorSettings
        {
            ResolutionFilter resolutions;
            unsigned ccd_size_divisor;
            unsigned logical_dpihw_override;
            unsigned pixel_count_multiplier;
            unsigned dpiset_override;
        };

        CustomSensorSettings custom_settings[] = {
            { { 900 }, 1, 900, 8, 150 },
            { { 1800 }, 1, 1800, 4, 300 },
            { { 3600 }, 1, 3600, 2, 600 },
            { { 7200 }, 1, 7200, 1, 1200 },
        };

        for (const CustomSensorSettings& setting : custom_settings) {
            sensor.resolutions = setting.resolutions;
            sensor.ccd_size_divisor = setting.ccd_size_divisor;
            sensor.logical_dpihw_override = setting.logical_dpihw_override;
            sensor.pixel_count_multiplier = setting.pixel_count_multiplier;
            sensor.dpiset_override = setting.dpiset_override;
            s_sensors->push_back(sensor);
        }
    }


    sensor = Genesys_Sensor();
    sensor.sensor_id = SensorId::CCD_PLUSTEK_OPTICFILM_7500I;
    sensor.optical_res = 7200;
    sensor.register_dpihw_override = 1200;
    sensor.black_pixels = 88; // TODO
    sensor.dummy_pixel = 20;
    sensor.ccd_start_xoffset = 0;
    sensor.fau_gain_white_ref = 210;
    sensor.gain_white_ref = 230;
    sensor.exposure = { 0x0000, 0x0000, 0x0000 };
    sensor.stagger_config = StaggerConfig{7200, 4};
    sensor.use_host_side_calib = true;
    sensor.custom_regs = {
        { 0x08, 0x00 },
        { 0x09, 0x00 },
        { 0x0a, 0x00 },
        { 0x16, 0x27 },
        { 0x17, 0x0c },
        { 0x18, 0x10 },
        { 0x19, 0x2a },
        { 0x1a, 0x00 },
        { 0x1b, 0x00 },
        { 0x1c, 0x20 },
        { 0x1d, 0x84 },
        { 0x52, 0x0a },
        { 0x53, 0x0d },
        { 0x54, 0x0f },
        { 0x55, 0x01 },
        { 0x56, 0x04 },
        { 0x57, 0x07 },
        { 0x58, 0x31 },
        { 0x59, 0x79 },
        { 0x5a, 0xc0 },
        { 0x70, 0x0c },
        { 0x71, 0x0d },
        { 0x72, 0x0e },
        { 0x73, 0x0f },
        { 0x74, 0x00 }, { 0x75, 0x00 }, { 0x76, 0x00 },
        { 0x77, 0x00 }, { 0x78, 0x00 }, { 0x79, 0x00 },
        { 0x7a, 0x00 }, { 0x7b, 0x00 }, { 0x7c, 0x00 },
    };
    sensor.gamma = { 1.0f, 1.0f, 1.0f };
    sensor.get_register_hwdpi_fun = default_get_register_hwdpi;
    sensor.get_hwdpi_divisor_fun = default_get_hwdpi_divisor_for_dpi;
    sensor.get_ccd_size_divisor_fun = get_ccd_size_divisor_exact;
    {
        struct CustomSensorSettings
        {
            ResolutionFilter resolutions;
            ScanMethod method;
            unsigned ccd_size_divisor;
            unsigned logical_dpihw_override;
            unsigned pixel_count_multiplier;
            unsigned exposure_lperiod;
            unsigned dpiset_override;
        };

        CustomSensorSettings custom_settings[] = {
            { { 900 }, ScanMethod::TRANSPARENCY, 1, 900, 8, 0x2f44, 150 },
            { { 1800 }, ScanMethod::TRANSPARENCY, 1, 1800, 4, 0x2f44, 300 },
            { { 3600 }, ScanMethod::TRANSPARENCY, 1, 3600, 2, 0x2f44, 600 },
            { { 7200 }, ScanMethod::TRANSPARENCY, 1, 7200, 1, 0x2f44, 1200 },
            { { 900 }, ScanMethod::TRANSPARENCY_INFRARED, 1, 900, 8, 0x2af8, 150 },
            { { 1800 }, ScanMethod::TRANSPARENCY_INFRARED, 1, 1800, 4, 0x2af8, 300 },
            { { 3600 }, ScanMethod::TRANSPARENCY_INFRARED, 1, 3600, 2, 0x2af8, 600 },
            { { 7200 }, ScanMethod::TRANSPARENCY_INFRARED, 1, 7200, 1, 0x2af8, 1200 },
        };

        for (const CustomSensorSettings& setting : custom_settings) {
            sensor.resolutions = setting.resolutions;
            sensor.method = setting.method;
            sensor.ccd_size_divisor = setting.ccd_size_divisor;
            sensor.logical_dpihw_override = setting.logical_dpihw_override;
            sensor.pixel_count_multiplier = setting.pixel_count_multiplier;
            sensor.exposure_lperiod = setting.exposure_lperiod;
            sensor.dpiset_override = setting.dpiset_override;
            s_sensors->push_back(sensor);
        }
    }


    sensor = Genesys_Sensor();
    sensor.sensor_id = SensorId::CCD_IMG101;
    sensor.resolutions = { 75, 100, 150, 300, 600, 1200 };
    sensor.exposure_lperiod = 11000;
    sensor.segment_size = 5136;
    sensor.segment_order = {0, 1};
    sensor.optical_res = 1200;
    sensor.black_pixels = 31;
    sensor.dummy_pixel = 31;
    sensor.ccd_start_xoffset = 0;
    sensor.fau_gain_white_ref = 210;
    sensor.gain_white_ref = 200;
    sensor.exposure = { 0x0000, 0x0000, 0x0000 };
    sensor.custom_regs = {
        { 0x16, 0xbb }, { 0x17, 0x13 }, { 0x18, 0x10 }, { 0x19, 0xff },
        { 0x1a, 0x34 }, { 0x1b, 0x00 }, { 0x1c, 0x20 }, { 0x1d, 0x06 },
        { 0x52, 0x02 }, { 0x53, 0x04 }, { 0x54, 0x06 }, { 0x55, 0x08 },
        { 0x56, 0x0a }, { 0x57, 0x00 }, { 0x58, 0x59 }, { 0x59, 0x31 }, { 0x5a, 0x40 },
        { 0x74, 0x00 }, { 0x75, 0x00 }, { 0x76, 0x3c },
        { 0x77, 0x00 }, { 0x78, 0x00 }, { 0x79, 0x9f },
        { 0x7a, 0x00 }, { 0x7b, 0x00 }, { 0x7c, 0x55 },
    };
    sensor.gamma = { 1.7f, 1.7f, 1.7f };
    sensor.get_register_hwdpi_fun = default_get_register_hwdpi;
    sensor.get_hwdpi_divisor_fun = default_get_hwdpi_divisor_for_dpi;
    sensor.get_ccd_size_divisor_fun = default_get_ccd_size_divisor_for_dpi;
    s_sensors->push_back(sensor);

    sensor = Genesys_Sensor();
    sensor.sensor_id = SensorId::CCD_PLUSTEK_OPTICBOOK_3800;
    sensor.resolutions = { 75, 100, 150, 300, 600, 1200 };
    sensor.exposure_lperiod = 11000;
    sensor.optical_res = 1200;
    sensor.black_pixels = 31;
    sensor.dummy_pixel = 31;
    sensor.ccd_start_xoffset = 0;
    sensor.fau_gain_white_ref = 210;
    sensor.gain_white_ref = 200;
    sensor.exposure = { 0, 0, 0 };
    sensor.custom_regs = {
        { 0x16, 0xbb }, { 0x17, 0x13 }, { 0x18, 0x10 }, { 0x19, 0xff },
        { 0x1a, 0x34 }, { 0x1b, 0x00 }, { 0x1c, 0x20 }, { 0x1d, 0x06 },
        { 0x52, 0x02 }, { 0x53, 0x04 }, { 0x54, 0x06 }, { 0x55, 0x08 },
        { 0x56, 0x0a }, { 0x57, 0x00 }, { 0x58, 0x59 }, { 0x59, 0x31 }, { 0x5a, 0x40 },
        { 0x74, 0x00 }, { 0x75, 0x00 }, { 0x76, 0x3c },
        { 0x77, 0x00 }, { 0x78, 0x00 }, { 0x79, 0x9f },
        { 0x7a, 0x00 }, { 0x7b, 0x00 }, { 0x7c, 0x55 },
    };
    sensor.gamma = { 1.7f, 1.7f, 1.7f };
    sensor.get_register_hwdpi_fun = default_get_register_hwdpi;
    sensor.get_hwdpi_divisor_fun = default_get_hwdpi_divisor_for_dpi;
    sensor.get_ccd_size_divisor_fun = default_get_ccd_size_divisor_for_dpi;
    s_sensors->push_back(sensor);


    sensor = Genesys_Sensor();
    sensor.sensor_id = SensorId::CIS_CANON_LIDE_80;
    sensor.optical_res = 1200; // real hardware limit is 2400
    sensor.ccd_size_divisor = 2;
    sensor.black_pixels = 20;
    sensor.dummy_pixel = 6;
    // tuned to give 3*8 multiple startx coordinate during shading calibration
    sensor.ccd_start_xoffset = 34; // 14=>3, 20=>2
    sensor.fau_gain_white_ref = 150;
    sensor.gain_white_ref = 150;
    // maps to 0x70-0x73 for GL841
    sensor.exposure = { 0x1000, 0x1000, 0x0500 };
    sensor.custom_regs = {
        { 0x08, 0x00 },
        { 0x09, 0x05 },
        { 0x0a, 0x07 },
        { 0x0b, 0x09 },
        { 0x16, 0x00 },
        { 0x17, 0x01 },
        { 0x18, 0x00 },
        { 0x19, 0x06 },
        { 0x1a, 0x00 },
        { 0x1b, 0x00 },
        { 0x1c, 0x00 },
        { 0x1d, 0x04 },
        { 0x52, 0x03 },
        { 0x53, 0x07 },
        { 0x54, 0x00 },
        { 0x55, 0x00 },
        { 0x56, 0x00 },
        { 0x57, 0x00 },
        { 0x58, 0x29 },
        { 0x59, 0x69 },
        { 0x5a, 0x55 },
        { 0x5b, 0x00 },
        { 0x5c, 0x00 },
        { 0x5d, 0x20 },
        { 0x5e, 0x41 },
    };
    sensor.gamma = { 1.0f, 1.0f, 1.0f };
    sensor.get_register_hwdpi_fun = default_get_register_hwdpi;
    sensor.get_hwdpi_divisor_fun = default_get_hwdpi_divisor_for_dpi;
    sensor.get_ccd_size_divisor_fun = default_get_ccd_size_divisor_for_dpi;
    s_sensors->push_back(sensor);
}

} // namespace genesys
