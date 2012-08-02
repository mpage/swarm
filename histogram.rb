require "histogram/array"
require "optparse"

def to_ms(nanos)
  nanos / 1_000_000
end

def read_data(path)
  data = {
    :ttcs   => [],
    :ttfbs  => [],
    :deltas => [],
  }

  File.open(path, "r") do |f|
    f.lines.each do |line|
      ttc, ttfb = line.strip.split(/\s+/)
      data[:ttcs] << to_ms(Integer(ttc))
      data[:ttfbs] << to_ms(Integer(ttfb))
      data[:deltas] << data[:ttfbs].last - data[:ttcs].last
    end
  end

  data
end

options = {
  :nbins    => 64,
  :bin_size => 50,
}

option_parser = OptionParser.new do |op|
  op.banner = <<-EOT
Usage: histogram [options] <ttfb|ttc|delta> [<name> <file>]+

Outputs histogram data suitable for use with gnuplot

EOT
  op.on("-b BINS", "Number of bins") do |nbins|
    options[:nbins] = Integer(nbins)
  end

  op.on("-s BIN_SIZE", "Bin size (in ms)") do |bin_size|
    options[:bin_size] = Float(bin_size)
  end
end

option_parser.parse!(ARGV)

if (ARGV.size <= 1) || ((ARGV.size - 1) % 2) > 0
  puts option_parser.help
  exit 1
end

type = (ARGV.shift + "s").to_sym
unless [:ttfbs, :ttcs, :deltas].include?(type)
  puts option_parser.help
  exit 1
end

series = {}
ARGV.each_with_index.chunk { |x| Integer(x[1] / 2).even? }.each do |_, x|
  series[x[0][0]] = read_data(x[1][0])
end

names = series.keys.sort
data = names.map { |n| series[n][type] }

bin_locs = (0..options[:nbins]).map { |ii| options[:bin_size] * ii }
bins, *freqs = data.first.histogram(bin_locs,
                                    :tp => :min,
                                    :other_sets => data.slice(1, data.size - 1))

fmt = "%10s"
title_parts = ["#         "].concat(names.map { |n| fmt % [n] })
puts title_parts.join(" ")

bins.each_with_index do |bin, ii|
  parts = [fmt % [bin]].concat(freqs.map { |freq| fmt % [freq[ii]] })
  puts parts.join(" ")
end
