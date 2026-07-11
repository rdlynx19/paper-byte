#pragma once
#include <Adafruit_MAX1704X.h>

// Wraps the MAX17048 fuel gauge on the Feather's STEMMA QT I2C bus. All
// readings are cached; update() re-reads from the gauge but throttles
// itself internally, so callers can invoke it on every redraw without
// worrying about hammering the I2C bus.
class Battery {
public:
    // Powers the STEMMA QT connector, brings up I2C, and probes for the
    // gauge. Safe to call again after waking from deep sleep (which resets
    // peripheral state). Returns false if no gauge is found — e.g. running
    // off USB only on a board without one — in which case every other
    // method is a no-op and available() stays false.
    bool begin();

    // Re-reads percent/charging state from the gauge, throttled to at most
    // once every UPDATE_INTERVAL_MS unless force is set. No-op if begin()
    // didn't find a gauge.
    void update(bool force = false);

    bool available()  const { return m_available; }
    int  percent()    const { return m_percent; }
    bool is_charging() const { return m_charging; }

private:
    // How often update() actually re-reads the gauge (percent + voltage).
    static const unsigned long UPDATE_INTERVAL_MS = 20000;

    // How often the charging trend check compares against a reference
    // voltage. The gauge's own charge-rate register turned out to be
    // unresponsive in practice, so charging is instead inferred from
    // whether cell voltage is actually rising — something a resting or
    // discharging LiPo cannot do on its own over a timescale this short,
    // making even a small real rise an unambiguous charging signal. Longer
    // than UPDATE_INTERVAL_MS so the comparison spans more than one noisy
    // ADC sample.
    static const unsigned long TREND_INTERVAL_MS = 60000;
    static constexpr float     TREND_RISE_THRESHOLD_V = 0.004f; // 4mV

    Adafruit_MAX17048 m_gauge;
    bool          m_available      = false;
    int           m_percent        = 0;
    bool          m_charging       = false;
    unsigned long m_last_update_ms = 0;
    float         m_trend_ref_v    = 0;
    unsigned long m_trend_ref_ms   = 0;
};
