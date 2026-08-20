//! Number and unit formatting for the workspaces.
//!
//! The core hands over raw counters; deciding that 1_536_000 bits/s reads as
//! "1.54 Mbit/s" is a presentation decision and belongs here, not in the
//! session.

pub fn integer(value: u64) -> String {
    let digits = value.to_string();
    let mut grouped = String::with_capacity(digits.len() + digits.len() / 3);
    for (index, digit) in digits.chars().enumerate() {
        if index > 0 && (digits.len() - index) % 3 == 0 {
            grouped.push(',');
        }
        grouped.push(digit);
    }
    grouped
}

pub fn fixed(value: f64, decimals: usize) -> String {
    format!("{value:.decimals$}")
}

pub fn percent(value: f64) -> String {
    format!("{value:.2} %")
}

pub fn megabits(value: f64) -> String {
    format!("{value:.3} Mbps")
}

pub fn bits_per_second(value: f64) -> String {
    const UNITS: [&str; 5] = ["bit/s", "kbit/s", "Mbit/s", "Gbit/s", "Tbit/s"];
    scale(value, 1000.0, &UNITS)
}

pub fn packets_per_second(value: f64) -> String {
    const UNITS: [&str; 4] = ["pps", "kpps", "Mpps", "Gpps"];
    scale(value, 1000.0, &UNITS)
}

/// Nanoseconds read as whatever unit keeps three significant digits: a 47 ns
/// kernel time and a 2.3 s run should both be legible in the same column.
pub fn duration_ns(value: u64) -> String {
    let value = value as f64;
    if value < 1_000.0 {
        return format!("{value:.0} ns");
    }
    if value < 1_000_000.0 {
        return format!("{:.2} us", value / 1_000.0);
    }
    if value < 1_000_000_000.0 {
        return format!("{:.2} ms", value / 1_000_000.0);
    }
    format!("{:.2} s", value / 1_000_000_000.0)
}

pub fn milliseconds(value: f64) -> String {
    format!("{value:.2} ms")
}

fn scale(value: f64, step: f64, units: &[&str]) -> String {
    if !value.is_finite() || value <= 0.0 {
        return format!("0 {}", units[0]);
    }
    let mut scaled = value;
    let mut unit = 0;
    while scaled >= step && unit + 1 < units.len() {
        scaled /= step;
        unit += 1;
    }
    if scaled >= 100.0 {
        format!("{scaled:.0} {}", units[unit])
    } else if scaled >= 10.0 {
        format!("{scaled:.1} {}", units[unit])
    } else {
        format!("{scaled:.2} {}", units[unit])
    }
}

/// IPv4-style protocol numbers the packet pane shows next to the digits.
pub fn protocol(number: u8) -> String {
    match number {
        1 => "ICMP".into(),
        6 => "TCP".into(),
        17 => "UDP".into(),
        0 => "-".into(),
        other => other.to_string(),
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn groups_thousands() {
        assert_eq!(integer(0), "0");
        assert_eq!(integer(999), "999");
        assert_eq!(integer(1_000), "1,000");
        assert_eq!(integer(1_234_567), "1,234,567");
    }

    #[test]
    fn scales_to_the_unit_that_stays_readable() {
        assert_eq!(bits_per_second(0.0), "0 bit/s");
        assert_eq!(bits_per_second(1_536_000.0), "1.54 Mbit/s");
        assert_eq!(packets_per_second(2_400_000.0), "2.40 Mpps");
        assert_eq!(duration_ns(47), "47 ns");
        assert_eq!(duration_ns(85_165), "85.17 us");
        assert_eq!(duration_ns(2_300_000_000), "2.30 s");
    }

    #[test]
    fn names_the_protocols_the_lab_generates() {
        assert_eq!(protocol(17), "UDP");
        assert_eq!(protocol(6), "TCP");
        assert_eq!(protocol(200), "200");
    }
}
