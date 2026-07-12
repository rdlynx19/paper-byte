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

    // Re-reads percent from the gauge, throttled to at most once every
    // UPDATE_INTERVAL_MS unless force is set. No-op if begin() didn't find
    // a gauge.
    void update(bool force = false);

    bool  available() const { return m_available; }
    int   percent()   const { return m_percent; }
    float voltage()   const { return m_voltage; }

private:
    static const unsigned long UPDATE_INTERVAL_MS = 20000;

    Adafruit_MAX17048 m_gauge;
    bool          m_available     = false;
    int           m_percent       = 0;
    float         m_voltage       = 0;
    unsigned long m_last_update_ms = 0;
};
