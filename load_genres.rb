#!/usr/bin/env ruby

require "bundler/inline"
require "erb"
require "open-uri"

gemfile do
  source "https://rubygems.org"
  gem "nokogiri"
  gem "i18n"
end

I18n.available_locales = [:en]

ID_PATTERN = /:playlist:([A-Za-z0-9]{22})/
Genre = Struct.new(:name, :id, :label, :color)

def build_genre(row)
  name = row.at("td.note:last").text
  link = row.at("a.note")[:href]
  id = link[ID_PATTERN, 1]
  label = row.at("td.note:first")[:title].to_s
    .sub("average duration", "")
    .sub(" bpm", "")
    .sub("%", "")
    .split(/\:\s+/).last
  style = row.at("td.note a")[:style].to_s
  r, g, b = *style[/color:\s+#([0-9a-f]{6})/i, 1].scan(/../).map { |c| c.to_i(16) }
  Genre.new(name, id, label, color_565(r, g, b))
end

def color_565(r, g, b)
  ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)
end

sorted_genres = Hash.new { [] }
%w[popularity].each do |vector|
  print "Fetching rankings by #{vector}... "
  doc = Nokogiri::HTML(URI.open("http://everynoise.com/everynoise1d.cgi?vector=#{vector}&scope=all"))
  sorted_genres[vector] = doc.css("body > table > tr").map(&method(:build_genre))
  puts "done"
end

print "Fetching countries... "
doc = Nokogiri::HTML(URI.open("http://everynoise.com/countries.html"))
countries = doc.css("td.column .country a")
  .map { |c| [c.text, c[:href].split(":").last] }
  .sort_by { |c| I18n.transliterate(c.first) }
  .to_h
puts "done"

genres = sorted_genres.values.first
alphabetical = genres.sort_by(&:name)
names = alphabetical.map(&:name)
suffix = genres.sort_by { |genre| genre.name.reverse }

template = <<-END_TEMPLATE
#define GENRE_COUNT <%= genres.size %>
#define COUNTRY_COUNT <%= countries.size %>

const char* genres[GENRE_COUNT] = { <%= names.map(&:inspect).join(", ") %> };

const char* genrePlaylists[GENRE_COUNT] = { <%= alphabetical.map { |g| g.id.inspect }.join(", ") %> };

const char* countries[COUNTRY_COUNT] = { <%= countries.keys.map(&:inspect).join(", ") %> };

const char* countryPlaylists[COUNTRY_COUNT] = { <%= countries.values.map(&:inspect).join(", ") %> };

const uint16_t genreColors[GENRE_COUNT] = { <%= alphabetical.map { |g| g.color.to_s }.join(", ") %> };

const uint16_t genreIndexes_suffix[GENRE_COUNT] = { <%= suffix.map { |g| names.index(g.name) }.join(", ") %> };

<% sorted_genres.each do |vector, sorted| -%>
const uint16_t genreIndexes_<%= vector %>[GENRE_COUNT] = { <%= sorted.map { |g| names.index(g.name) }.join(", ") %> };

<% if sorted.first.label -%>
const char* genreLabels_<%= vector %>[GENRE_COUNT] = { <%= sorted.map { |g| g.label.inspect }.join(", ") %> };
<% end -%>
<% end -%>

END_TEMPLATE
erb = ERB.new(template, 0, "-")

File.open("src/genres.h", "w") do |f|
  f << erb.result(binding)
end
