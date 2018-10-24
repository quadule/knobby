#!/usr/bin/env ruby

require "open-uri"
require "nokogiri"
require "erb"

ID_PATTERN = /:playlist:([A-Za-z0-9]{22})/
Genre = Struct.new(:name, :id, :label)

def build_genre(row)
  name = row.at("td.note:last").text
  link = row.at("a.note")[:href]
  label = row.at("td.note:first")[:title].to_s
    .sub("average duration", "")
    .sub(" bpm", "")
    .sub("%", "")
    .split(/\:\s+/).last
  id = link[ID_PATTERN, 1]
  Genre.new(name, id, label)
end

sorted_genres = Hash.new { [] }
%w[popularity modernity background tempo].each do |vector|
  print "Fetching rankings by #{vector}... "
  doc = Nokogiri::HTML(open("http://everynoise.com/everynoise1d.cgi?vector=#{vector}&scope=all"))
  sorted_genres[vector] = doc.css("body > table > tr").map(&method(:build_genre))
  puts "done"
end

genres = sorted_genres.values.first
alphabetical = genres.sort_by(&:name)
names = alphabetical.map(&:name)
suffix = genres.sort_by { |genre| genre.name.reverse }
sorted_genres["tempo"].reverse!

template = <<-END_TEMPLATE
#define GENRE_COUNT <%= genres.size %>

const char* genres[GENRE_COUNT] = { <%= names.map(&:inspect).join(", ") %> };

const char* playlists[GENRE_COUNT] = { <%= alphabetical.map { |g| g.id.inspect }.join(", ") %> };

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
