//
// Copyright 2011,2014 Ettus Research LLC
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

#ifndef INCLUDED_B100_CLOCK_CTRL_HPP
#define INCLUDED_B100_CLOCK_CTRL_HPP

#include <shd/types/serial.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/utility.hpp>
#include <vector>

/*!
 * The B100 clock control:
 * - Disable/enable clock lines.
 */
class b100_clock_ctrl : boost::noncopyable{
public:
    typedef boost::shared_ptr<b100_clock_ctrl> sptr;

    virtual ~b100_clock_ctrl(void) = 0;

    /*!
     * Make a new clock control object.
     * \param iface the controller iface object
     * \param master_clock_rate the master FPGA/sample clock rate
     * \return the clock control object
     */
    static sptr make(shd::i2c_iface::sptr iface, double master_clock_rate);

    /*!
     * Set the rate of the fpga clock line.
     * Throws if rate is not valid.
     * \param rate the new rate in Hz
     */
    virtual void set_fpga_clock_rate(double rate) = 0;

    /*!
     * Get the rate of the fpga clock line.
     * \return the fpga clock rate in Hz
     */
    virtual double get_fpga_clock_rate(void) = 0;

    /*!
     * Get the possible rates of the rx dboard clock.
     * \return a vector of clock rates in Hz
     */
    virtual std::vector<double> get_rx_dboard_clock_rates(void) = 0;

    /*!
     * Get the possible rates of the tx dboard clock.
     * \return a vector of clock rates in Hz
     */
    virtual std::vector<double> get_tx_dboard_clock_rates(void) = 0;

    /*!
     * Set the rx dboard clock rate to a possible rate.
     * \param rate the new clock rate in Hz
     * \throw exception when rate cannot be achieved
     */
    virtual void set_rx_dboard_clock_rate(double rate) = 0;

    /*!
     * Set the tx dboard clock rate to a possible rate.
     * \param rate the new clock rate in Hz
     * \throw exception when rate cannot be achieved
     */
    virtual void set_tx_dboard_clock_rate(double rate) = 0;

    /*!
     * Get the current rx dboard clock rate.
     * \return the clock rate in Hz
     */
    virtual double get_rx_clock_rate(void) = 0;

    /*!
     * Get the current tx dboard clock rate.
     * \return the clock rate in Hz
     */
    virtual double get_tx_clock_rate(void) = 0;
    
    /*!
     * Enable/disable the FPGA clock.
     * \param enb true to enable
     */
    
    virtual void enable_fpga_clock(bool enb) = 0;

    /*!
     * Enable/disable the rx dboard clock.
     * \param enb true to enable
     */
    virtual void enable_rx_dboard_clock(bool enb) = 0;

    /*!
     * Enable/disable the tx dboard clock.
     * \param enb true to enable
     */
    virtual void enable_tx_dboard_clock(bool enb) = 0;
    
    /*!
     * Use the internal TCXO reference
     */
    virtual void use_internal_ref(void) = 0;
    
    /*!
     * Use the external SMA reference
     */
    virtual void use_external_ref(void) = 0;
    
    /*!
     * Use external if available, internal otherwise
     */
    virtual void use_auto_ref(void) = 0;

    //! Is the reference locked?
    virtual bool get_locked(void) = 0;

};

#endif /* INCLUDED_B100_CLOCK_CTRL_HPP */
