#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#ifndef UNIT_TEST
  
  
#endif

class ZipFile;

class EpubTocEntry
{
public:
  std::string title;
  std::string href;
  std::string anchor;
  int level;
  EpubTocEntry(std::string title, std::string href, std::string anchor, int level) : title(title), href(href), anchor(anchor), level(level) {}
};

class Epub
{
private:
  // the title read from the EPUB meta data
  std::string m_title;
  // the author (dc:creator) read from the EPUB meta data
  std::string m_author;
  // the cover image
  std::string m_cover_image_item;
  // the ncx file
  std::string m_toc_ncx_item;
  // where is the EPUBfile?
  std::string m_path;
  // the spine of the EPUB file
  std::vector<std::pair<std::string, std::string>> m_spine;
  // the toc of the EPUB file
  std::vector<EpubTocEntry> m_toc;
  // the base path for items in the EPUB file
  std::string m_base_path;
  // find the path for the content.opf file
  bool find_content_opf_file(ZipFile &zip, std::string &content_opf_file);
  bool parse_content_opf(ZipFile &zip, std::string &content_opf_file);
  bool parse_toc_ncx_file(ZipFile &zip);

public:
  Epub(const std::string &path);
  ~Epub() {}
  std::string &get_base_path() { return m_base_path; }
  bool load();

  const std::string &get_path() const { return m_path; }
  const std::string &get_title();
  const std::string &get_author();
  const std::string &get_cover_image_item();
  uint8_t *get_item_contents(const std::string &item_href, size_t *size = nullptr);

  std::string &get_spine_item(int spine_index);
  int get_spine_item_id(std::string spine_key);
  int get_spine_items_count();

  EpubTocEntry &get_toc_item(int toc_index);
  int get_toc_items_count();
  // work out the section index for a toc index
  int get_spine_index_for_toc_index(int toc_index);
  // work out which toc entry covers a given spine index — the toc entry
  // with the largest spine index <= spine_index, i.e. the most recent
  // chapter boundary at or before that position. Returns -1 if the toc is
  // empty or spine_index is before the first toc entry.
  int get_toc_index_for_spine_index(int spine_index);
};