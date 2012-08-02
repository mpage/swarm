require "array_stats"

def to_ms(nanos)
  "%0.2f" % [nanos / 1_000_000]
end

ttcs = []
ttfbs = []
deltas = []

STDIN.lines.each do |line|
  ttc, ttfb = line.strip.split(/\s+/)
  ttcs << Integer(ttc)
  ttfbs << Integer(ttfb)
  deltas << ttfbs.last - ttcs.last
end

ttcs.sort!
ttfbs.sort!
deltas.sort!

fmt = "%5s %20s %20s %20s"

puts fmt % ["", "connect", "first byte", "delta"]
[50, 75, 90, 95, 99, 99.9].each do |p|
  puts fmt % [p,
              to_ms(ttcs.percentile(p)),
              to_ms(ttfbs.percentile(p)),
              to_ms(deltas.percentile(p))]
end
