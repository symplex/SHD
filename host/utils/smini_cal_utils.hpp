//
// Copyright 2011-2012,2014 Ettus Research LLC
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.
//

#include <shd/utils/paths.hpp>
#include <shd/property_tree.hpp>
#include <shd/smini/multi_smini.hpp>
#include <shd/smini/dboard_eeprom.hpp>
#include <shd/utils/paths.hpp>
#include <shd/utils/algorithm.hpp>
#include <shd/utils/msg.hpp>
#include <boost/filesystem.hpp>
#include <boost/format.hpp>
#include <iostream>
#include <vector>
#include <complex>
#include <cmath>
#include <cstdlib>
#include <fstream>

namespace fs = boost::filesystem;

struct result_t{double freq, real_corr, imag_corr, best, delta;};

typedef std::complex<float> samp_type;

/***********************************************************************
 * Constants
 **********************************************************************/
static const double tau = 6.28318531;
static const size_t wave_table_len = 65536;
static const size_t num_search_steps = 5;
static const double default_precision = 0.0001;
static const double default_freq_step = 7.3e6;
static const size_t default_fft_bin_size = 1000;

/***********************************************************************
 * Set standard defaults for devices
 **********************************************************************/
static inline void set_optimum_defaults(shd::smini::multi_smini::sptr smini)
{
    shd::property_tree::sptr tree = smini->get_device()->get_tree();
    // Will work on 1st subdev, top-level must make sure it's the right one
    shd::smini::subdev_spec_t subdev_spec = smini->get_rx_subdev_spec();

    const shd::fs_path mb_path = "/mboards/0";
    const std::string mb_name = tree->access<std::string>(mb_path / "name").get();
    if (mb_name.find("SMINI2") != std::string::npos or
        mb_name.find("N200") != std::string::npos or
        mb_name.find("N210") != std::string::npos or
        mb_name.find("X300") != std::string::npos or
        mb_name.find("X310") != std::string::npos)
    {
        smini->set_tx_rate(12.5e6);
        smini->set_rx_rate(12.5e6);
    }
    else if (mb_name.find("B100") != std::string::npos)
    {
        smini->set_tx_rate(4e6);
        smini->set_rx_rate(4e6);
    }
    else if (mb_name.find("E100") != std::string::npos or mb_name.find("E110") != std::string::npos)
    {
        smini->set_tx_rate(4e6);
        smini->set_rx_rate(8e6);
    }
    else
    {
        throw std::runtime_error("self-calibration is not supported for this device");
    }

    const shd::fs_path tx_fe_path = "/mboards/0/dboards/" + subdev_spec[0].db_name + "/tx_frontends/0";
    const std::string tx_name = tree->access<std::string>(tx_fe_path / "name").get();
    if (tx_name.find("WBX") == std::string::npos and
        tx_name.find("SBX") == std::string::npos and
        tx_name.find("CBX") == std::string::npos and
        tx_name.find("RFX") == std::string::npos and
        tx_name.find("UBX") == std::string::npos
        )
    {
        throw std::runtime_error("self-calibration is not supported for this TX dboard");
    }
    smini->set_tx_gain(0);

    const shd::fs_path rx_fe_path = "/mboards/0/dboards/" + subdev_spec[0].db_name + "/rx_frontends/0";
    const std::string rx_name = tree->access<std::string>(rx_fe_path / "name").get();
    if (rx_name.find("WBX") == std::string::npos and
        rx_name.find("SBX") == std::string::npos and
        rx_name.find("CBX") == std::string::npos and
        rx_name.find("RFX") == std::string::npos and
        rx_name.find("UBX") == std::string::npos
        )
    {
        throw std::runtime_error("self-calibration is not supported for this RX dboard");
    }
    smini->set_rx_gain(0);
}

/***********************************************************************
 * Check for empty serial
 **********************************************************************/
void check_for_empty_serial(shd::smini::multi_smini::sptr smini)
{
    // Will work on 1st subdev, top-level must make sure it's the right one
    shd::smini::subdev_spec_t subdev_spec = smini->get_rx_subdev_spec();

    //extract eeprom
    shd::property_tree::sptr tree = smini->get_device()->get_tree();
    // This only works with transceiver boards, so we can always check rx side
    const shd::fs_path db_path = "/mboards/0/dboards/" + subdev_spec[0].db_name + "/rx_eeprom";
    const shd::smini::dboard_eeprom_t db_eeprom = tree->access<shd::smini::dboard_eeprom_t>(db_path).get();

    std::string error_string = "This dboard has no serial!\n\nPlease see the Calibration documentation for details on how to fix this.";
    if (db_eeprom.serial.empty())
        throw std::runtime_error(error_string);
}

/***********************************************************************
 * Sinusoid wave table
 **********************************************************************/
class wave_table
{
public:
    wave_table(const double ampl)
    {
        _table.resize(wave_table_len);
        for (size_t i = 0; i < wave_table_len; i++)
            _table[i] = samp_type(std::polar(ampl, (tau*i)/wave_table_len));
    }

    inline samp_type operator()(const size_t index) const
    {
        return _table[index % wave_table_len];
    }

private:
    std::vector<samp_type > _table;
};

/***********************************************************************
 * Compute power of a tone
 **********************************************************************/
static inline double compute_tone_dbrms(
    const std::vector<samp_type> &samples,
    const double freq)  //freq is fractional
{
    //shift the samples so the tone at freq is down at DC
    //and average the samples to measure the DC component
    samp_type average = 0;
    for (size_t i = 0; i < samples.size(); i++)
        average += samp_type(std::polar(1.0, -freq*tau*i)) * samples[i];

    return 20*std::log10(std::abs(average/float(samples.size())));
}

/***********************************************************************
 * Write a dat file
 **********************************************************************/
static inline void write_samples_to_file(
    const std::vector<samp_type > &samples, const std::string &file)
{
    std::ofstream outfile(file.c_str(), std::ofstream::binary);
    outfile.write((const char*)&samples.front(), samples.size()*sizeof(samp_type));
    outfile.close();
}


/***********************************************************************
 * Retrieve d'board serial
 **********************************************************************/
static std::string get_serial(
    shd::smini::multi_smini::sptr smini,
    const std::string &tx_rx)
{
    shd::property_tree::sptr tree = smini->get_device()->get_tree();
    // Will work on 1st subdev, top-level must make sure it's the right one
    shd::smini::subdev_spec_t subdev_spec = smini->get_rx_subdev_spec();
    const shd::fs_path db_path = "/mboards/0/dboards/" + subdev_spec[0].db_name + "/" + tx_rx + "_eeprom";
    const shd::smini::dboard_eeprom_t db_eeprom = tree->access<shd::smini::dboard_eeprom_t>(db_path).get();
    return db_eeprom.serial;
}

/***********************************************************************
 * Store data to file
 **********************************************************************/
static void store_results(
    const std::vector<result_t> &results,
    const std::string &XX, // "TX" or "RX"
    const std::string &xx, // "tx" or "rx"
    const std::string &what, // Type of test, e.g. "iq",
    const std::string &serial)
{
    //make the calibration file path
    fs::path cal_data_path = fs::path(shd::get_app_path()) / ".shd";
    fs::create_directory(cal_data_path);
    cal_data_path = cal_data_path / "cal";
    fs::create_directory(cal_data_path);
    cal_data_path = cal_data_path / str(boost::format("%s_%s_cal_v0.2_%s.csv") % xx % what % serial);
    if (fs::exists(cal_data_path))
        fs::rename(cal_data_path, cal_data_path.string() + str(boost::format(".%d") % time(NULL)));

    //fill the calibration file
    std::ofstream cal_data(cal_data_path.string().c_str());
    cal_data << boost::format("name, %s Frontend Calibration\n") % XX;
    cal_data << boost::format("serial, %s\n") % serial;
    cal_data << boost::format("timestamp, %d\n") % time(NULL);
    cal_data << boost::format("version, 0, 1\n");
    cal_data << boost::format("DATA STARTS HERE\n");
    cal_data << "lo_frequency, correction_real, correction_imag, measured, delta\n";

    for (size_t i = 0; i < results.size(); i++)
    {
        cal_data
            << results[i].freq << ", "
            << results[i].real_corr << ", "
            << results[i].imag_corr << ", "
            << results[i].best << ", "
            << results[i].delta << "\n"
        ;
    }

    std::cout << "wrote cal data to " << cal_data_path << std::endl;
}

/***********************************************************************
 * Data capture routine
 **********************************************************************/
static void capture_samples(
    shd::smini::multi_smini::sptr smini,
    shd::rx_streamer::sptr rx_stream,
    std::vector<samp_type > &buff,
    const size_t nsamps_requested)
{
    buff.resize(nsamps_requested);
    shd::rx_metadata_t md;

    // Right after the stream is started, there will be transient data.
    // That transient data is discarded and only "good" samples are returned.
    size_t nsamps_to_discard = size_t(smini->get_rx_rate() * 0.001); // 1ms to be discarded
    std::vector<samp_type> discard_buff(nsamps_to_discard);

    shd::stream_cmd_t stream_cmd(shd::stream_cmd_t::STREAM_MODE_NUM_SAMPS_AND_DONE);
    stream_cmd.num_samps = buff.size() + nsamps_to_discard;
    stream_cmd.stream_now = true;
    smini->issue_stream_cmd(stream_cmd);
    size_t num_rx_samps = 0;

    // Discard the transient samples.
    rx_stream->recv(&discard_buff.front(), discard_buff.size(), md);
    if (md.error_code != shd::rx_metadata_t::ERROR_CODE_NONE)
    {
        throw std::runtime_error(str(boost::format(
            "Receiver error: %s"
        ) % md.strerror()));
    }

    // Now capture the data we want
    num_rx_samps = rx_stream->recv(&buff.front(), buff.size(), md);

    //validate the received data
    if (md.error_code != shd::rx_metadata_t::ERROR_CODE_NONE)
    {
        throw std::runtime_error(str(boost::format(
            "Receiver error: %s"
        ) % md.strerror()));
    }

    //we can live if all the data didnt come in
    if (num_rx_samps > buff.size()/2)
    {
        buff.resize(num_rx_samps);
        return;
    }
    if (num_rx_samps != buff.size())
        throw std::runtime_error("did not get all the samples requested");
}

/***********************************************************************
 * Setup function
 **********************************************************************/
static shd::smini::multi_smini::sptr setup_smini_for_cal(std::string &args, std::string &subdev, std::string &serial)
{
    std::cout << std::endl;
    std::cout << boost::format("Creating the smini device with: %s...") % args << std::endl;
    shd::smini::multi_smini::sptr smini = shd::smini::multi_smini::make(args);

    // Configure subdev
    if (!subdev.empty())
    {
        smini->set_tx_subdev_spec(subdev);
        smini->set_rx_subdev_spec(subdev);
    }
    SHD_MSG(status) << "Running calibration for " << smini->get_tx_subdev_name(0) << std::endl;
    serial = get_serial(smini, "tx");
    SHD_MSG(status) << "Daughterboard serial: " << serial << std::endl;

    //set the antennas to cal
    if (not shd::has(smini->get_rx_antennas(), "CAL") or not shd::has(smini->get_tx_antennas(), "CAL"))
        throw std::runtime_error("This board does not have the CAL antenna option, cannot self-calibrate.");
    smini->set_rx_antenna("CAL");
    smini->set_tx_antenna("CAL");

    //fail if daughterboard has no serial
    check_for_empty_serial(smini);

    //set optimum defaults
    set_optimum_defaults(smini);

    return smini;
}

/***********************************************************************
 * Function to find optimal RX gain setting (for the current frequency)
 **********************************************************************/
SHD_INLINE void set_optimal_rx_gain(
    shd::smini::multi_smini::sptr smini,
    shd::rx_streamer::sptr rx_stream,
    double wave_freq = 0.0)
{
    const double gain_step = 3.0;
    const double gain_compression_threshold = gain_step * 0.5;
    const double actual_rx_rate = smini->get_rx_rate();
    const double actual_tx_freq = smini->get_tx_freq();
    const double actual_rx_freq = smini->get_rx_freq();
    const double bb_tone_freq = actual_tx_freq - actual_rx_freq + wave_freq;
    const size_t nsamps = size_t(actual_rx_rate / default_fft_bin_size);

    std::vector<samp_type> buff(nsamps);
    shd::gain_range_t rx_gain_range = smini->get_rx_gain_range();
    double rx_gain = rx_gain_range.start() + gain_step;
    double curr_dbrms = 0.0;
    double prev_dbrms = 0.0;
    double delta = 0.0;

    // No sense in setting the gain where this is no gain range
    if (rx_gain_range.stop() - rx_gain_range.start() < gain_step)
        return;

    // The algorithm below cycles through the RX gain range
    // looking for the point where the signal begins to get
    // clipped and the gain begins to be compressed.  It does
    // this by looking for the gain setting where the increase
    // in the tone is less than the gain step by more than the
    // gain compression threshold (curr - prev < gain - threshold).

    // Initialize prev_dbrms value
    smini->set_rx_gain(rx_gain);
    capture_samples(smini, rx_stream, buff, nsamps);
    prev_dbrms = compute_tone_dbrms(buff, bb_tone_freq/actual_rx_rate);
    rx_gain += gain_step;

    // Find RX gain where signal begins to clip
    while (rx_gain <= rx_gain_range.stop())
    {
        smini->set_rx_gain(rx_gain);
        capture_samples(smini, rx_stream, buff, nsamps);
        curr_dbrms = compute_tone_dbrms(buff, bb_tone_freq/actual_rx_rate);
        delta = curr_dbrms - prev_dbrms;

        // check if the gain is compressed beyone the threshold
        if (delta < gain_step - gain_compression_threshold)
            break;  // if so, we are done

        prev_dbrms = curr_dbrms;
        rx_gain += gain_step;
    }

    // The rx_gain value at this point is the gain setting where clipping
    // occurs or the gain setting that is just beyond the gain range.
    // The gain is reduced by 2 steps to make sure it is within the range and
    // under the point where it is clipped with enough room to make adjustments.
    rx_gain -= 2 * gain_step;

    // Make sure the gain is within the range.
    rx_gain = rx_gain_range.clip(rx_gain);

    // Finally, set the gain.
    smini->set_rx_gain(rx_gain);
}

