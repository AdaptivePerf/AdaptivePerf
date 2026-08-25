// SPDX-FileCopyrightText: 2026 CERN
// SPDX-License-Identifier: LGPL-3.0-or-later

#include "tui.hpp"
#include "print.hpp"
#include "adaptyst/socket.hpp"
#include <ftxui/component/app.hpp>
#include <ftxui/component/captured_mouse.hpp>
#include <ftxui/component/component.hpp>
#include <ftxui/component/component_base.hpp>
#include <ftxui/component/component_options.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/component/mouse.hpp>
#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/box.hpp>
#include <ftxui/screen/string.hpp>
#include <algorithm>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <limits>
#include <memory>
#include <string>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>
#include <unistd.h>
#include <set>

#define STR_WRAP "Wrap"
#define STR_OUTPUT_WAIT "Waiting for output..."
#define STR_NEW_WINDOW "New window"
#define STR_EXIT "Exit Adaptyst"
#define STR_INCREASE_SIZE "Increase terminal size"
#define STR_VIEWPORT_PART1 "The viewport is too small for "
#define STR_VIEWPORT_PART2 " open window(s)."
#define STR_STATUS "Current status:"
#define STR_COPYRIGHT "Copyright (C) CERN. Core licensed under GNU LGPL v3+."
#define STR_COPYRIGHT_SHORT "(C) CERN."
#define STR_INCREASE_SIZE_LONG "Increase terminal size: there's no room for another window."
#define STR_OUTPUT_SELECT "Select output"
#define STR_YES "Yes"
#define STR_NO "No"
#define STR_ANALYSIS_RUNNING "Performance analysis is still running."
#define STR_EXIT_CONFIRMATION "Are you sure you want to exit Adaptyst?"

namespace adaptyst {
  namespace {
    namespace fs = std::filesystem;
    using namespace ftxui;

    class DynamicComponent : public ComponentBase {
    public:
      void set(Component component) {
        this->DetachAllChildren();
        this->Add(std::move(component));
      }

      Element OnRender() override {
        return this->ChildCount() ? this->ChildAt(0)->Render() : filler();
      }
    };

    enum class CaptureResult {
      Ignored,
      Handled,
      Capture,
    };

    class CaptureEventComponent : public ComponentBase {
    private:
      std::function<CaptureResult(Event)> handler;
      CapturedMouse captured_mouse;

    public:
      CaptureEventComponent(Component child,
                            std::function<CaptureResult(Event)> handler)
        : handler(std::move(handler)) {
        this->Add(std::move(child));
      }

      bool OnEvent(Event event) override {
        if (this->captured_mouse) {
          this->handler(event);

          if (event.is_mouse() && event.mouse().motion == Mouse::Released) {
            this->captured_mouse.reset();
          }

          return true;
        }

        CaptureResult result = this->handler(event);

        if (result == CaptureResult::Capture) {
          this->captured_mouse = this->CaptureMouse(event);
        }

        return result != CaptureResult::Ignored ||
          ComponentBase::OnEvent(std::move(event));
      }
    };
  }

  class Tui::Impl {
  private:
    struct Source {
      std::string label;
      fs::path path;
    };

    enum class LineStyle {
      Plain,
      Major,
      Sub,
      MajorError,
      SubError,
    };

    struct TextSegment {
      std::string text;
      fs::path source;
      LineStyle style = LineStyle::Plain;
      bool source_directory = false;
    };

    using TextLine = std::vector<TextSegment>;

    struct Pane {
      enum class Scrollbar {
        None,
        Horizontal,
        Vertical,
      };

      struct Link {
        fs::path source;
        bool directory;
        int line;
        int x_min;
        int x_max;
        Box box;
      };

      Source source;
      std::string display_label;
      bool wrap = true;
      bool follow_tail = true;
      int scroll_x = 0;
      int scroll_y = 0;
      int max_scroll_x = 0;
      int max_scroll_y = 0;
      int viewport_width = 1;
      int viewport_height = 1;
      Scrollbar dragged_scrollbar = Scrollbar::None;
      bool dragged_content = false;
      int content_drag_x = 0;
      int content_drag_scroll_x = 0;
      std::uintmax_t file_offset = 0;
      std::string partial_line;
      std::vector<std::string> lines;
      std::vector<std::unique_ptr<Link>> links;
      int link_flash_ticks = 0;
      int selected_link = -1;
      int selected_control = 0;
      int close_control_index = -1;
      int link_control_index = -1;
      Box window_box;
      Box content_box;
      Component controls;
      Component component;
    };

    struct SplitState {
      Direction direction;
      int size;
      int min_size;
      int max_size;
      int back_min_size;
      Box box;
    };

    struct PaneLayout {
      Component component;
      int min_width;
      int min_height;
    };

    Terminal &terminal;
    bool formatted;
    std::string version;
    fs::path main_log_path;
    unsigned int max_read_size;
    int max_lines;
    std::unique_ptr<FileDescriptor> log_source_fd;
    std::set<fs::path> exported_sources;
    std::set<fs::path> exported_directories;
    std::string status;
    std::uintmax_t status_file_offset = 0;
    App app = App::Fullscreen();
    std::atomic<bool> finished = false;
    bool successful = false;
    bool aborted = false;
    bool picker_shown = false;
    bool picker_adds_pane = true;
    bool confirmation_shown = false;
    bool viewport_too_small = false;
    int capacity_notice_ticks = 0;
    int root_selected = 0;
    int picker_selected = 0;
    bool picker_shows_source = false;
    fs::path picker_directory;
    Box picker_menu_box;
    Box picker_view_box;
    std::weak_ptr<Pane> picker_target;
    std::vector<Source> sources;
    std::vector<Source> picker_sources;
    std::vector<std::string> source_labels;
    std::vector<std::shared_ptr<Pane>> panes;
    std::vector<std::shared_ptr<SplitState>> split_states;
    std::shared_ptr<DynamicComponent> tile_component;
    Box tile_box;
    int layout_width = 0;
    int layout_height = 0;
    Component add_button;
    Component exit_button;
    Component picker_menu_component;
    Component root;

    static constexpr int wrap_control_width = 6;
    static constexpr int button_control_width = 3;
    static constexpr int max_controls_width = wrap_control_width +
      5 * button_control_width;
    static constexpr int min_title_width = 5;
    static constexpr int min_pane_width = 2 + max_controls_width +
      min_title_width;
    static constexpr int min_pane_height = 5;
    static constexpr int preferred_pane_aspect = 2;
    static constexpr int root_chrome_height = 7;
    static constexpr int top_controls_width = 15;

    static std::string ellipsize(std::string text, int width) {
      if (width <= 0) {
        return "";
      }

      if (string_width(text) <= width) {
        return text;
      }

      if (width <= 3) {
        return std::string(width, '.');
      }

      std::string result;
      int result_width = 0;
      int content_width = width - 3;

      for (auto &glyph : Utf8ToGlyphs(text)) {
        int glyph_width = string_width(glyph);

        if (result_width + glyph_width > content_width) {
          break;
        }

        result += glyph;
        result_width += glyph_width;
      }

      return result + "...";
    }

    static std::string source_label(const fs::path &path) {
      std::string label;

      for (auto &part : path.parent_path()) {
        if (!label.empty()) {
          label += " -> ";
        }

        label += part.string();
      }

      std::string name = path.stem().string();

      if (!label.empty()) {
        label += " -> ";
      }

      return label + name;
    }

    static bool path_is_within(const fs::path &path, const fs::path &directory) {
      auto path_normal = path.lexically_normal();
      auto directory_normal = directory.lexically_normal();
      auto path_part = path_normal.begin();
      auto path_end = path_normal.end();

      for (auto &directory_part : directory_normal) {
        if (path_part == path_end || *path_part != directory_part) {
          return false;
        }

        path_part++;
      }

      return true;
    }

    void refresh_exported_sources() {
      if (!this->log_source_fd) {
        return;
      }

      std::vector<std::string> read_lines;

      try {
        std::string line;

        while (!(line = this->log_source_fd->read()).empty()) {
          read_lines.push_back(line);
        }
      } catch (...) {}

      std::size_t separator;

      for (auto &line : read_lines) {
        fs::path path(line);

        if (this->exported_sources.find(path) ==
            this->exported_sources.end()) {
          this->exported_sources.insert(path);

          fs::path root = fs::path(this->terminal.get_log_dir()).lexically_normal();
          fs::path directory = path.parent_path().lexically_normal();

          while (directory != root && path_is_within(directory, root)) {
            if (this->exported_directories.find(directory) ==
                this->exported_directories.end()) {
              this->exported_directories.insert(directory);
            }

            fs::path parent = directory.parent_path();

            if (parent == directory) {
              break;
            }

            directory = std::move(parent);
          }
        }
      }
    }

    void refresh_sources() {
      this->refresh_exported_sources();
      fs::path selected_path;

      if (this->picker_selected >= 0 &&
          this->picker_selected < static_cast<int>(this->picker_sources.size())) {
        selected_path = this->picker_sources[this->picker_selected].path;
      }

      std::vector<Source> discovered = {{"Main", this->main_log_path}};
      fs::path root_path(this->terminal.get_log_dir());

      for (auto &source_path : this->exported_sources) {
        std::error_code error;
        fs::path relative = fs::relative(source_path, root_path, error);

        if (!error) {
          discovered.push_back({source_label(relative), source_path});
        }
      }

      std::sort(discovered.begin() + 1, discovered.end(),
                [](const Source &left, const Source &right) {
                  return left.label < right.label;
                });
      this->sources = std::move(discovered);
      this->picker_sources.clear();
      this->source_labels.clear();

      for (auto &source : this->sources) {
        if (this->picker_directory.empty() ||
            path_is_within(source.path, this->picker_directory)) {
          this->picker_sources.push_back(source);
          this->source_labels.push_back(source.label);
        }
      }

      this->picker_selected = 0;

      for (std::size_t i = 0; i < this->picker_sources.size(); i++) {
        if (this->picker_sources[i].path == selected_path) {
          this->picker_selected = i;
          break;
        }
      }
    }

    static void reset_pane(const std::shared_ptr<Pane> &pane, Source source) {
      pane->source = std::move(source);
      pane->scroll_x = 0;
      pane->scroll_y = 0;
      pane->follow_tail = true;
      pane->file_offset = 0;
      pane->partial_line.clear();
      pane->lines.clear();
    }

    void refresh_pane(Pane &pane) {
      if (pane.source.path.empty()) {
        return;
      }

      std::error_code error;
      auto size = fs::file_size(pane.source.path, error);

      if (error) {
        return;
      }

      if (size < pane.file_offset) {
        pane.file_offset = 0;
        pane.partial_line.clear();
        pane.lines.clear();
      }

      if (size == pane.file_offset) {
        return;
      }

      bool truncated = size - pane.file_offset > this->max_read_size;

      if (truncated) {
        pane.file_offset = size - this->max_read_size;
        pane.partial_line.clear();
        pane.lines.clear();
      }

      std::ifstream stream(pane.source.path, std::ios::binary);
      stream.seekg(pane.file_offset);
      std::string data((std::istreambuf_iterator<char>(stream)),
                       std::istreambuf_iterator<char>());
      pane.file_offset += data.size();
      pane.partial_line += data;

      if (truncated) {
        auto newline = pane.partial_line.find('\n');
        pane.partial_line.erase(0, newline == std::string::npos
                                ? pane.partial_line.size() : newline + 1);
      }

      std::size_t newline;

      while ((newline = pane.partial_line.find('\n')) != std::string::npos) {
        std::string line = pane.partial_line.substr(0, newline);

        if (!line.empty() && line.back() == '\r') {
          line.pop_back();
        }

        pane.lines.push_back(std::move(line));
        pane.partial_line.erase(0, newline + 1);
      }

      if (pane.lines.size() > this->max_lines) {
        pane.lines.erase(pane.lines.begin(),
                         pane.lines.end() - this->max_lines);
      }

      // constexpr std::size_t max_line_size = 1024 * 1024;

      // if (pane.partial_line.size() > max_line_size) {
      //   pane.partial_line.erase(0, pane.partial_line.size() - max_line_size);
      // }
    }

    void refresh_status() {
      std::error_code error;
      auto size = fs::file_size(this->main_log_path, error);

      if (error) {
        return;
      }

      if (size < this->status_file_offset) {
        this->status_file_offset = 0;
        this->status.clear();
      }

      if (size == this->status_file_offset) {
        return;
      }

      bool truncated = size - this->status_file_offset > this->max_read_size;

      if (truncated) {
        this->status_file_offset = size - this->max_read_size;
        this->status.clear();
      }

      std::ifstream stream(this->main_log_path, std::ios::binary);
      stream.seekg(this->status_file_offset);
      std::string line;

      if (truncated) {
        if (!std::getline(stream, line) || stream.eof()) {
          return;
        }
        this->status_file_offset = stream.tellg();
      }

      while (true) {
        auto line_offset = stream.tellg();

        if (!std::getline(stream, line)) {
          break;
        }

        if (stream.eof()) {
          this->status_file_offset = line_offset;
          break;
        }

        this->status_file_offset = stream.tellg();

        if (!line.empty() && line.back() == '\r') {
          line.pop_back();
        }

        line_style(line);

        if (line.starts_with("==> ")) {
          this->status = line.substr(4);
        }
      }
    }

    void refresh() {
      auto [width, height] = this->tile_dimensions();

      if (width != this->layout_width || height != this->layout_height) {
        this->rebuild_tiles();
      } else {
        this->update_split_constraints();
      }

      this->viewport_too_small = static_cast<int>(this->panes.size()) >
        this->window_capacity();
      this->root_selected = this->viewport_too_small ? 1 : 0;

      if (this->viewport_too_small) {
        this->picker_shown = false;
      }

      if (this->capacity_notice_ticks > 0) {
        this->capacity_notice_ticks--;
      }

      this->refresh_sources();

      for (auto &pane : this->panes) {
        this->refresh_pane(*pane);

        if (pane->link_flash_ticks > 0) {
          pane->link_flash_ticks--;
        }
      }

      this->refresh_status();
    }

    static LineStyle line_style(std::string &line) {
      bool error = line.starts_with("[ERROR] ");

      if (error) {
        line.erase(0, 8);
      }

      if (line.starts_with("==> ")) {
        return error ? LineStyle::MajorError : LineStyle::Major;
      }

      if (line.starts_with("-> ")) {
        return error ? LineStyle::SubError : LineStyle::Sub;
      }

      return error ? LineStyle::SubError : LineStyle::Plain;
    }

    Element style_element(Element element, LineStyle style) const {
      if (!this->formatted) {
        return element;
      }

      if (style == LineStyle::Major) {
        return element | color(Color::Green) | bold;
      }

      if (style == LineStyle::Sub) {
        return element | color(Color::Cyan);
      }

      if (style == LineStyle::MajorError) {
        return element | color(Color::Red) | bold;
      }

      if (style == LineStyle::SubError) {
        return element | color(Color::Red);
      }

      return element;
    }

    TextLine linkify(std::string line, LineStyle style) {
      TextLine result;
      std::size_t offset = 0;

      while (offset < line.size()) {
        fs::path match_path;
        bool match_directory = false;
        std::size_t match_offset = std::string::npos;
        std::size_t match_size = 0;

        for (auto &source : this->sources) {
          std::string path = source.path.string();

          if (path.empty()) {
            continue;
          }

          std::size_t found = line.find(path, offset);

          if (found < match_offset ||
              (found == match_offset && path.size() > match_size)) {
            match_path = source.path;
            match_directory = false;
            match_offset = found;
            match_size = path.size();
          }
        }

        for (auto &directory : this->exported_directories) {
          std::string path = directory.string();
          std::size_t found = line.find(path, offset);

          if (found < match_offset ||
              (found == match_offset && path.size() > match_size)) {
            match_path = directory;
            match_directory = true;
            match_offset = found;
            match_size = path.size();
          }
        }

        if (match_path.empty() || match_offset == std::string::npos) {
          result.push_back({line.substr(offset), {}, style});
          break;
        }

        if (match_offset > offset) {
          result.push_back({line.substr(offset, match_offset - offset), {}, style});
        }

        result.push_back({line.substr(match_offset, match_size), match_path,
            style, match_directory});
        offset = match_offset + match_size;
      }

      if (result.empty()) {
        result.push_back({std::move(line), {}, style});
      }

      return result;
    }

    static TextLine slice_line(const TextLine &line, std::size_t begin,
                               std::size_t end) {
      TextLine result;
      std::size_t segment_begin = 0;

      for (auto &segment : line) {
        std::size_t segment_end = segment_begin + segment.text.size();
        std::size_t overlap_begin = std::max(begin, segment_begin);
        std::size_t overlap_end = std::min(end, segment_end);

        if (overlap_begin < overlap_end) {
          result.push_back({segment.text.substr(overlap_begin - segment_begin,
                                                overlap_end - overlap_begin),
              segment.source, segment.style,
              segment.source_directory});
        }

        segment_begin = segment_end;
      }

      return result;
    }

    std::vector<TextLine> pane_lines(const Pane &pane) {
      std::vector<std::string> lines = pane.lines;

      if (!pane.partial_line.empty()) {
        lines.push_back(pane.partial_line);
      }

      if (lines.empty()) {
        return {};
      }

      if (!pane.wrap) {
        std::vector<TextLine> linked;
        linked.reserve(lines.size());

        for (auto &line : lines) {
          LineStyle style = line_style(line);
          linked.push_back(this->linkify(std::move(line), style));
        }

        return linked;
      }

      int width = pane.viewport_width;

      if (width <= 1) {
        width = 80;
      }

      std::vector<TextLine> wrapped;

      for (auto &line : lines) {
        LineStyle style = line_style(line);
        TextLine linked = this->linkify(std::move(line), style);
        std::string plain;

        for (auto &segment : linked) {
          plain += segment.text;
        }

        if (plain.empty()) {
          wrapped.push_back(std::move(linked));
          continue;
        }

        std::size_t begin = 0;
        constexpr char whitespace[] = " \t\f\v\r";

        while (plain.size() - begin > static_cast<std::size_t>(width)) {
          std::size_t hard_end = begin + width;
          std::size_t split = plain.find_last_of(whitespace, hard_end - 1);
          std::size_t end = split != std::string::npos && split > begin
            ? split : hard_end;
          wrapped.push_back(slice_line(linked, begin, end));

          begin = plain.find_first_not_of(whitespace, end);
          if (begin == std::string::npos) {
            break;
          }
        }

        if (begin != std::string::npos && begin < plain.size()) {
          wrapped.push_back(slice_line(linked, begin, plain.size()));
        }
      }

      return wrapped;
    }

    static void drag_scrollbar(Pane &pane, const Mouse &mouse) {
      if (pane.dragged_scrollbar == Pane::Scrollbar::Vertical) {
        int position = std::clamp(mouse.y - pane.content_box.y_min,
                                  0, pane.viewport_height);
        pane.scroll_y = position * pane.max_scroll_y / pane.viewport_height;
        pane.follow_tail = pane.scroll_y == pane.max_scroll_y;
      } else if (pane.dragged_scrollbar == Pane::Scrollbar::Horizontal) {
        int range = std::max(1, pane.viewport_width - 1);
        int position = std::clamp(mouse.x - pane.content_box.x_min, 0, range);
        pane.scroll_x = position * pane.max_scroll_x / range;
      }
    }

    bool scroll(Pane &pane, Event event) {
      auto adjust_x = [&pane](int amount) {
        pane.scroll_x = std::clamp(pane.scroll_x + amount,
                                   0, pane.max_scroll_x);
      };
      auto adjust_y = [&pane](int amount) {
        pane.scroll_y = std::clamp(pane.scroll_y + amount,
                                   0, pane.max_scroll_y);
        pane.follow_tail = pane.scroll_y == pane.max_scroll_y;
      };

      if (event == Event::ArrowUp || event == Event::k) {
        adjust_y(-1);
      } else if (event == Event::ArrowDown || event == Event::j) {
        adjust_y(1);
      } else if (event == Event::PageUp) {
        adjust_y(-pane.viewport_height);
      } else if (event == Event::PageDown) {
        adjust_y(pane.viewport_height);
      } else if (event == Event::Home) {
        pane.scroll_y = 0;
        pane.follow_tail = false;
      } else if (event == Event::End) {
        pane.scroll_y = pane.max_scroll_y;
        pane.follow_tail = true;
      } else if (!pane.wrap && (event == Event::ArrowLeft || event == Event::h)) {
        adjust_x(-1);
      } else if (!pane.wrap && (event == Event::ArrowRight || event == Event::l)) {
        adjust_x(1);
      } else if (event.is_mouse() &&
                 pane.content_box.Contain(event.mouse().x, event.mouse().y)) {
        auto &mouse = event.mouse();

        bool horizontal_wheel = !pane.wrap && pane.max_scroll_x > 0 &&
          (mouse.shift || pane.max_scroll_y == 0 ||
           mouse.y == pane.content_box.y_max);

        if (horizontal_wheel && mouse.button == Mouse::WheelUp) {
          adjust_x(-3);
        } else if (horizontal_wheel && mouse.button == Mouse::WheelDown) {
          adjust_x(3);
        } else if (mouse.button == Mouse::WheelUp) {
          adjust_y(-3);
        } else if (mouse.button == Mouse::WheelDown) {
          adjust_y(3);
        } else if (!pane.wrap && mouse.button == Mouse::WheelLeft) {
          adjust_x(-3);
        } else if (!pane.wrap && mouse.button == Mouse::WheelRight) {
          adjust_x(3);
        } else {
          return false;
        }
      } else {
        return false;
      }

      return true;
    }

    void open_picker(std::shared_ptr<Pane> pane = nullptr,
                     fs::path directory = {}) {
      this->picker_directory = std::move(directory);
      this->picker_shows_source = !this->picker_directory.empty();
      this->refresh_sources();
      this->picker_target = pane;
      this->picker_adds_pane = !pane && !this->picker_shows_source;
      this->picker_selected = 0;

      if (pane) {
        for (std::size_t i = 0; i < this->sources.size(); i++) {
          if (this->sources[i].path == pane->source.path) {
            this->picker_selected = i;
            break;
          }
        }
      }

      this->picker_shown = true;

      if (this->picker_menu_component) {
        this->picker_menu_component->TakeFocus();
      }
    }

    void choose_source() {
      if (this->picker_selected < 0 ||
          this->picker_selected >= static_cast<int>(this->picker_sources.size())) {
        return;
      }

      Source source = this->picker_sources[this->picker_selected];

      if (this->picker_shows_source) {
        this->show_source_pane(source.path);
      } else if (this->picker_adds_pane) {
        this->add_pane(std::move(source));
      } else if (auto pane = this->picker_target.lock()) {
        reset_pane(pane, std::move(source));
      }

      this->picker_shown = false;
      this->picker_directory.clear();
      this->picker_shows_source = false;
    }

    void show_source_pane(const fs::path &path) {
      auto pane = std::find_if(this->panes.begin(), this->panes.end(),
                               [&path](const std::shared_ptr<Pane> &candidate) {
                                 return candidate->source.path == path;
                               });

      if (pane != this->panes.end()) {
        (*pane)->link_flash_ticks = 6;
        return;
      }

      auto source = std::find_if(this->sources.begin(), this->sources.end(),
                                 [&path](const Source &candidate) {
                                   return candidate.path == path;
                                 });

      if (source != this->sources.end()) {
        this->add_pane(*source);
      }
    }

    void activate_link(fs::path source, bool directory) {
      this->app.Post([this, source = std::move(source), directory] {
        if (directory) {
          this->open_picker(nullptr, source);
        } else {
          this->show_source_pane(source);
        }
      });
    }

    static void select_link(Pane &pane, int selected) {
      if (selected < 0 || selected >= static_cast<int>(pane.links.size())) {
        pane.selected_link = -1;
        return;
      }

      pane.selected_link = selected;
      auto &link = *pane.links[selected];

      if (link.line < pane.scroll_y) {
        pane.scroll_y = link.line;
      } else if (link.line >= pane.scroll_y + pane.viewport_height) {
        pane.scroll_y = link.line - pane.viewport_height + 1;
      }

      pane.scroll_y = std::clamp(pane.scroll_y, 0, pane.max_scroll_y);
      pane.follow_tail = pane.scroll_y == pane.max_scroll_y;

      if (!pane.wrap) {
        if (link.x_min < pane.scroll_x) {
          pane.scroll_x = link.x_min;
        } else if (link.x_max >= pane.scroll_x + pane.viewport_width) {
          pane.scroll_x = link.x_max - pane.viewport_width + 1;
        }

        pane.scroll_x = std::clamp(pane.scroll_x, 0, pane.max_scroll_x);
      }
    }

    static int link_in_direction(const Pane &pane, int horizontal,
                                 int vertical) {
      if (pane.selected_link < 0 ||
          pane.selected_link >= static_cast<int>(pane.links.size())) {
        return -1;
      }

      auto &current = *pane.links[pane.selected_link];
      int current_x = current.x_min + current.x_max;
      int target = -1;
      int best_primary_distance = 0;
      int best_secondary_distance = 0;

      for (std::size_t i = 0; i < pane.links.size(); i++) {
        if (static_cast<int>(i) == pane.selected_link) {
          continue;
        }

        auto &candidate = *pane.links[i];
        int delta_x = candidate.x_min + candidate.x_max - current_x;
        int delta_y = candidate.line - current.line;

        if ((horizontal != 0 && horizontal * delta_x <= 0) ||
            (vertical != 0 && vertical * delta_y <= 0)) {
          continue;
        }

        int primary_distance = std::abs(horizontal != 0 ? delta_x : delta_y);
        int secondary_distance = std::abs(horizontal != 0 ? delta_y : delta_x);

        if (target == -1 || primary_distance < best_primary_distance ||
            (primary_distance == best_primary_distance &&
             secondary_distance < best_secondary_distance)) {
          target = static_cast<int>(i);
          best_primary_distance = primary_distance;
          best_secondary_distance = secondary_distance;
        }
      }

      return target;
    }

    std::shared_ptr<Pane> pane_in_direction(const std::shared_ptr<Pane> &pane,
                                            int horizontal, int vertical) {
      auto current = std::find(this->panes.begin(), this->panes.end(), pane);

      if (current == this->panes.end()) {
        return nullptr;
      }

      int center_x = pane->window_box.x_min + pane->window_box.x_max;
      int center_y = pane->window_box.y_min + pane->window_box.y_max;
      std::shared_ptr<Pane> target;
      long long best_score = std::numeric_limits<long long>::max();

      for (auto &candidate : this->panes) {
        if (candidate == pane) {
          continue;
        }

        int delta_x = candidate->window_box.x_min +
          candidate->window_box.x_max - center_x;
        int delta_y = candidate->window_box.y_min +
          candidate->window_box.y_max - center_y;

        if ((horizontal != 0 && horizontal * delta_x <= 0) ||
            (vertical != 0 && vertical * delta_y <= 0)) {
          continue;
        }

        long long score = static_cast<long long>(delta_x) * delta_x +
          static_cast<long long>(delta_y) * delta_y;

        if (!target || score < best_score) {
          target = candidate;
          best_score = score;
        }
      }

      return target;
    }

    void move_pane(const std::shared_ptr<Pane> &pane, int horizontal,
                   int vertical) {
      auto target_pane = this->pane_in_direction(pane, horizontal, vertical);

      if (!target_pane) {
        return;
      }

      auto current = std::find(this->panes.begin(), this->panes.end(), pane);
      auto target = std::find(this->panes.begin(), this->panes.end(),
                              target_pane);
      std::iter_swap(current, target);
      this->rebuild_tiles();
    }

    void close_pane(const std::shared_ptr<Pane> &pane) {
      if (this->panes.size() == 1) {
        if (this->finished.load(std::memory_order_acquire)) {
          this->app.Exit();
        } else {
          this->confirmation_shown = true;
        }
        return;
      }

      std::erase(this->panes, pane);
      this->rebuild_tiles();
    }

    void focus_pane(const std::shared_ptr<Pane> &pane, int direction) {
      auto current = std::find(this->panes.begin(), this->panes.end(), pane);
      auto index = std::distance(this->panes.begin(), current) + direction;

      if (index < 0 || index >= static_cast<decltype(index)>(this->panes.size())) {
        (direction > 0 ? this->add_button : this->exit_button)->TakeFocus();
        return;
      }

      auto &target = this->panes[index];

      if (direction < 0 && !target->links.empty()) {
        target->selected_control = target->link_control_index;
        select_link(*target, static_cast<int>(target->links.size()) - 1);
      } else {
        target->selected_link = -1;
        target->selected_control = direction > 0 ? 0 :
          target->close_control_index;
      }

      target->controls->TakeFocus();
    }

    Component make_pane(const std::shared_ptr<Pane> &pane) {
      std::weak_ptr<Pane> weak = pane;
      pane->display_label = pane->source.label;

      auto source = Button(&pane->display_label, [this, weak] {
          if (auto locked = weak.lock()) {
            this->open_picker(std::move(locked));
          }
        }, ButtonOption::Ascii());
      auto wrap = Checkbox(STR_WRAP, &pane->wrap);

      auto left = Button("<", [this, weak] {
        if (auto locked = weak.lock()) {
          this->app.Post([this, locked] { this->move_pane(locked, -1, 0); });
        }
      }, ButtonOption::Ascii());
      auto up = Button("^", [this, weak] {
        if (auto locked = weak.lock()) {
          this->app.Post([this, locked] { this->move_pane(locked, 0, -1); });
        }
      }, ButtonOption::Ascii());
      auto down = Button("v", [this, weak] {
        if (auto locked = weak.lock()) {
          this->app.Post([this, locked] { this->move_pane(locked, 0, 1); });
        }
      }, ButtonOption::Ascii());
      auto right = Button(">", [this, weak] {
        if (auto locked = weak.lock()) {
          this->app.Post([this, locked] { this->move_pane(locked, 1, 0); });
        }
      }, ButtonOption::Ascii());
      auto close = Button("x", [this, weak] {
        if (auto locked = weak.lock()) {
          this->app.Post([this, locked] { this->close_pane(locked); });
        }
      }, ButtonOption::Ascii());

      auto link_focus = Button("", [] {}, ButtonOption::Ascii());

      auto left_control = Maybe(left, [this, weak] {
        auto locked = weak.lock();
        return locked && this->pane_in_direction(locked, -1, 0);
      });
      auto up_control = Maybe(up, [this, weak] {
        auto locked = weak.lock();
        return locked && this->pane_in_direction(locked, 0, -1);
      });
      auto down_control = Maybe(down, [this, weak] {
        auto locked = weak.lock();
        return locked && this->pane_in_direction(locked, 0, 1);
      });
      auto right_control = Maybe(right, [this, weak] {
        auto locked = weak.lock();
        return locked && this->pane_in_direction(locked, 1, 0);
      });
      auto spacer = Renderer([] { return filler(); });

      auto control_container =
        Container::Horizontal({source, spacer, wrap, left_control, up_control, down_control,
            right_control, close, link_focus},
          &pane->selected_control);

      pane->close_control_index = close->Index();
      pane->link_control_index = link_focus->Index();

      auto controls =
        Renderer(control_container,
                 [this, weak, source, wrap, left_control,
                  up_control, down_control, right_control,
                  close] {
                   auto locked = weak.lock();

                   if (!locked) {
                     return filler();
                   }

                   int pane_width = locked->window_box.x_max - locked->window_box.x_min + 1;

                   if (pane_width <= 1) {
                     pane_width = min_pane_width;
                   }

                   int visible_moves = !!this->pane_in_direction(locked, -1, 0) +
                     !!this->pane_in_direction(locked, 0, -1) +
                     !!this->pane_in_direction(locked, 0, 1) +
                     !!this->pane_in_direction(locked, 1, 0);
                   int controls_width = wrap_control_width + button_control_width +
                     visible_moves * button_control_width;
                   int title_width = std::max(min_title_width,
                                              pane_width - 2 - controls_width);
                   locked->display_label = ellipsize(locked->source.label,
                                                     title_width - 2);

                   return hbox({
                       source->Render(),
                       filler(),
                       wrap->Render() | size(WIDTH, EQUAL, wrap_control_width),
                       left_control->Render(),
                       up_control->Render(),
                       down_control->Render(),
                       right_control->Render(),
                       close->Render() | size(WIDTH, EQUAL, button_control_width),
                     });
                 });

      pane->controls = controls;

      auto content = Renderer([this, weak] {
        auto locked = weak.lock();

        if (!locked) {
          return filler();
        }

        auto lines = this->pane_lines(*locked);
        Elements elements;
        elements.reserve(lines.size());
        locked->links.clear();
        int content_width = 0;

        for (std::size_t line_index = 0; line_index < lines.size(); line_index++) {
          auto &line = lines[line_index];
          Elements segments;
          int line_width = 0;

          for (auto &segment : line) {
            int segment_x = line_width;
            line_width += segment.text.size();
            Element element = this->style_element(text(segment.text),
                                                  segment.style);

            if (!segment.source.empty()) {
              auto link = std::make_unique<Pane::Link>();
              link->source = segment.source;
              link->directory = segment.source_directory;
              link->line = static_cast<int>(line_index);
              link->x_min = segment_x;
              link->x_max = line_width - 1;
              element = element | underlined;

              if (this->formatted) {
                element = element | color(Color::BlueLight);
              }

              if (static_cast<int>(locked->links.size()) ==
                  locked->selected_link) {
                element = element | inverted;
              }

              element = element | reflect(link->box);
              locked->links.push_back(std::move(link));
            }

            segments.push_back(std::move(element));
          }

          content_width = std::max(content_width, line_width);
          elements.push_back(hbox(std::move(segments)));
        }

        if (locked->selected_link >= static_cast<int>(locked->links.size())) {
          locked->selected_link = -1;

          if (locked->selected_control == locked->link_control_index) {
            locked->selected_control = locked->close_control_index;
          }
        }

        locked->viewport_width = std::max(1, locked->content_box.x_max -
                                          locked->content_box.x_min);
        locked->viewport_height = std::max(1, locked->content_box.y_max -
                                           locked->content_box.y_min);
        locked->max_scroll_x = locked->wrap ? 0 :
          std::max(0, content_width - locked->viewport_width);
        locked->max_scroll_y =
          std::max(0, static_cast<int>(lines.size()) - locked->viewport_height);

        locked->scroll_x = std::clamp(locked->scroll_x, 0,
                                      locked->max_scroll_x);
        locked->scroll_y = locked->follow_tail ? locked->max_scroll_y :
          std::clamp(locked->scroll_y, 0, locked->max_scroll_y);

        Element log = vbox(std::move(elements)) |
          focusPosition(locked->scroll_x + locked->viewport_width / 2,
                        locked->scroll_y + locked->viewport_height / 2) |
          hscroll_indicator | vscroll_indicator | frame | flex |
          reflect(locked->content_box);

        if (lines.empty()) {
          return vbox({
              text(STR_OUTPUT_WAIT) | dim | hcenter,
              separator(),
              std::move(log) | flex,
            });
        }

        return log;
      });

      auto renderer = Renderer(controls, [weak, controls, content] {
        auto locked = weak.lock();

        if (!locked) {
          return filler();
        }

        int title_width = std::max(1, locked->window_box.x_max -
                                   locked->window_box.x_min - 1);
        Element title = controls->Render() | size(WIDTH, EQUAL, title_width);

        if (locked->link_flash_ticks > 0 &&
            locked->link_flash_ticks % 2 == 0) {
          title = std::move(title) | inverted;
        }

        return window(std::move(title),
                      content->Render()) | reflect(locked->window_box);
      });

      return Make<CaptureEventComponent>(renderer, [this, weak, controls](Event event) {
        auto locked = weak.lock();

        if (!locked) {
          return CaptureResult::Ignored;
        }

        if (event.is_mouse()) {
          auto &mouse = event.mouse();

          if (locked->dragged_scrollbar != Pane::Scrollbar::None) {
            drag_scrollbar(*locked, mouse);

            if (mouse.motion == Mouse::Released) {
              locked->dragged_scrollbar = Pane::Scrollbar::None;
            }

            return CaptureResult::Handled;
          }

          if (locked->dragged_content) {
            locked->scroll_x =
              std::clamp(locked->content_drag_scroll_x + locked->content_drag_x - mouse.x,
                         0, locked->max_scroll_x);

            if (mouse.motion == Mouse::Released) {
              locked->dragged_content = false;
            }

            return CaptureResult::Handled;
          }

          if (mouse.button == Mouse::Left && mouse.motion == Mouse::Pressed) {
            for (auto &link : locked->links) {
              if (link->box.Contain(mouse.x, mouse.y)) {
                fs::path source = link->source;
                bool directory = link->directory;
                this->activate_link(std::move(source), directory);
                return CaptureResult::Handled;
              }
            }
          }

          if (locked->content_box.Contain(mouse.x, mouse.y)) {
            controls->TakeFocus();
            if (mouse.button == Mouse::Left && mouse.motion == Mouse::Pressed) {
              if (locked->max_scroll_y > 0 &&
                  mouse.x == locked->content_box.x_max) {
                locked->dragged_scrollbar = Pane::Scrollbar::Vertical;
              } else if (locked->max_scroll_x > 0 &&
                         mouse.y == locked->content_box.y_max) {
                locked->dragged_scrollbar = Pane::Scrollbar::Horizontal;
              }

              if (locked->dragged_scrollbar != Pane::Scrollbar::None) {
                drag_scrollbar(*locked, mouse);
                return CaptureResult::Capture;
              }

              if (!locked->wrap && locked->max_scroll_x > 0) {
                locked->dragged_content = true;
                locked->content_drag_x = mouse.x;
                locked->content_drag_scroll_x = locked->scroll_x;
                return CaptureResult::Capture;
              }
            }
          }
        } else if (!controls->Focused()) {
          return CaptureResult::Ignored;
        }

        if (locked->selected_control == locked->link_control_index) {
          if (event == Event::Tab) {
            if (locked->selected_link + 1 <
                static_cast<int>(locked->links.size())) {
              select_link(*locked, locked->selected_link + 1);
            } else {
              locked->selected_link = -1;
              this->focus_pane(locked, 1);
            }

            return CaptureResult::Handled;
          }

          if (event == Event::TabReverse) {
            if (locked->selected_link > 0) {
              select_link(*locked, locked->selected_link - 1);
            } else {
              locked->selected_link = -1;
              locked->selected_control = locked->close_control_index;
            }

            return CaptureResult::Handled;
          }

          if (event == Event::Return && locked->selected_link >= 0 &&
              locked->selected_link < static_cast<int>(locked->links.size())) {
            auto &link = *locked->links[locked->selected_link];
            this->activate_link(link.source, link.directory);
            return CaptureResult::Handled;
          }

          int target = -1;
          bool arrow = true;

          if (event == Event::ArrowLeft) {
            target = link_in_direction(*locked, -1, 0);
          } else if (event == Event::ArrowRight) {
            target = link_in_direction(*locked, 1, 0);
          } else if (event == Event::ArrowUp) {
            target = link_in_direction(*locked, 0, -1);
          } else if (event == Event::ArrowDown) {
            target = link_in_direction(*locked, 0, 1);
          } else {
            arrow = false;
          }

          if (target != -1) {
            select_link(*locked, target);
          }

          if (arrow) {
            return CaptureResult::Handled;
          }
        }

        if (event == Event::Tab &&
            locked->selected_control == locked->close_control_index) {
          if (!locked->links.empty()) {
            locked->selected_control = locked->link_control_index;
            select_link(*locked, 0);
          } else {
            this->focus_pane(locked, 1);
          }

          return CaptureResult::Handled;
        }

        if (event == Event::TabReverse && locked->selected_control == 0) {
          this->focus_pane(locked, -1);
          return CaptureResult::Handled;
        }

        if (event == Event::ArrowLeftCtrl) {
          this->app.Post([this, locked] { this->move_pane(locked, -1, 0); });
          return CaptureResult::Handled;
        }

        if (event == Event::ArrowRightCtrl) {
          this->app.Post([this, locked] { this->move_pane(locked, 1, 0); });
          return CaptureResult::Handled;
        }

        if (event == Event::ArrowUpCtrl) {
          this->app.Post([this, locked] { this->move_pane(locked, 0, -1); });
          return CaptureResult::Handled;
        }

        if (event == Event::ArrowDownCtrl) {
          this->app.Post([this, locked] { this->move_pane(locked, 0, 1); });
          return CaptureResult::Handled;
        }

        if (event == Event::w) {
          locked->wrap = !locked->wrap;
          return CaptureResult::Handled;
        }

        return this->scroll(*locked, std::move(event))
          ? CaptureResult::Handled : CaptureResult::Ignored;
      });
    }

    void add_pane(Source source) {
      if (!this->panes.empty() &&
          static_cast<int>(this->panes.size()) >= this->window_capacity()) {
        this->capacity_notice_ticks = 15;
        return;
      }

      auto pane = std::make_shared<Pane>();

      pane->source = std::move(source);
      pane->component = this->make_pane(pane);
      this->refresh_pane(*pane);
      this->panes.push_back(std::move(pane));
      this->rebuild_tiles();
    }

    std::pair<int, int> raw_tile_dimensions() {
      int width = this->app.dimx();
      int height = this->app.dimy() - root_chrome_height;

      if (width <= 1) {
        width = this->tile_box.x_max - this->tile_box.x_min + 1;
      }

      if (height <= 1) {
        height = this->tile_box.y_max - this->tile_box.y_min + 1;
      }

      return {std::max(1, width), std::max(1, height)};
    }

    std::pair<int, int> tile_dimensions() {
      auto [width, height] = this->raw_tile_dimensions();
      return {std::max(min_pane_width, width),
        std::max(min_pane_height, height)};
    }

    int window_capacity() {
      auto [width, height] = this->raw_tile_dimensions();
      int columns = (width + 1) / (min_pane_width + 1);
      int rows = (height + 1) / (min_pane_height + 1);
      return columns * rows;
    }

    PaneLayout build_layout(std::size_t begin, std::size_t end,
                            int width, int height,
                            std::vector<std::shared_ptr<SplitState>> &states) {
      if (end - begin <= 1) {
        return {this->panes[begin]->component, min_pane_width,
          min_pane_height};
      }

      bool can_split_vertically = width >= 2 * min_pane_width + 1;
      bool can_split_horizontally = height >= 2 * min_pane_height + 1;

      // Prefer log panes that are roughly twice as wide as they are tall.
      bool vertical = !can_split_horizontally ||
        (can_split_vertically && width >= preferred_pane_aspect * height);

      std::size_t middle = begin + (end - begin) / 2;
      int count = static_cast<int>(end - begin);
      int main_count = static_cast<int>(middle - begin);
      int dimension = vertical ? width : height;
      int main_size = std::max(1, (dimension - 1) * main_count / count);

      PaneLayout main =
        this->build_layout(begin, middle,
                           vertical ? main_size : width,
                           vertical ? height : main_size, states);
      PaneLayout back =
        this->build_layout(middle, end,
                           vertical ? std::max(1, width - main_size - 1) : width,
                           vertical ? height : std::max(1, height - main_size - 1), states);

      auto state = std::make_shared<SplitState>();
      state->direction = vertical ? Direction::Left : Direction::Up;
      state->min_size = vertical ? main.min_width : main.min_height;
      state->back_min_size = vertical ? back.min_width : back.min_height;
      state->max_size =
        std::max(state->min_size, dimension - state->back_min_size - 1);
      state->size = std::clamp(main_size, state->min_size,
                               state->max_size);

      ResizableSplitOption options;
      options.main = std::move(main.component);
      options.back = std::move(back.component);
      options.direction = state->direction;
      options.main_size = &state->size;
      options.min = state->min_size;
      options.max = &state->max_size;
      Component split = ResizableSplit(std::move(options));
      Component rendered = Renderer(split, [split, state] {
        return split->Render() | reflect(state->box);
      });
      states.push_back(state);

      return {
        std::move(rendered),
        vertical ? main.min_width + back.min_width + 1
        : std::max(main.min_width, back.min_width),
        vertical ? std::max(main.min_height, back.min_height)
        : main.min_height + back.min_height + 1,
      };
    }

    void update_split_constraints() {
      for (auto &state : this->split_states) {
        int dimension = state->direction == Direction::Left
          ? state->box.x_max - state->box.x_min + 1
          : state->box.y_max - state->box.y_min + 1;

        if (dimension <= 1) {
          continue;
        }

        state->max_size =
          std::max(state->min_size, dimension - state->back_min_size - 1);
        state->size = std::clamp(state->size, state->min_size,
                                 state->max_size);
      }
    }

    void rebuild_tiles() {
      auto [width, height] = this->tile_dimensions();
      std::vector<std::shared_ptr<SplitState>> states;
      PaneLayout layout = this->build_layout(0, this->panes.size(),
                                             width, height, states);
      this->tile_component->set(std::move(layout.component));
      this->split_states = std::move(states);
      this->layout_width = width;
      this->layout_height = height;
    }

    void request_exit() {
      if (this->finished.load(std::memory_order_acquire)) {
        this->app.Exit();
      } else {
        this->confirmation_shown = true;
      }
    }

    void exit_immediately() {
      this->aborted = !this->finished.load(std::memory_order_acquire);
      this->app.Exit();
    }

    Component make_root() {
      auto add = Button(STR_NEW_WINDOW, [this] { this->open_picker(); },
                        ButtonOption::Ascii());
      auto exit = Button(STR_EXIT, [this] { this->request_exit(); },
                         ButtonOption::Ascii());

      this->add_button = add;
      this->exit_button = exit;

      auto resize_exit = Button(STR_EXIT, [this] {
        this->exit_immediately();
      }, ButtonOption::Ascii());
      auto top_controls = Container::Vertical({add, exit});
      auto normal_container = Container::Vertical({top_controls,
          this->tile_component});
      auto warning_container = Container::Vertical({resize_exit});
      auto main_container = Container::Tab({normal_container, warning_container},
                                           &this->root_selected);
      auto main_renderer =
        Renderer(main_container,
                 [this, add, exit, resize_exit] {
                   if (this->viewport_too_small) {
                     Element warning_status = paragraphAlignCenter(this->status) |
                       size(WIDTH, LESS_THAN, 60) | hcenter;

                     if (this->finished.load(std::memory_order_acquire) && this->formatted) {
                       warning_status = std::move(warning_status) |
                         color(this->successful ? Color::Green : Color::Red) | bold;
                     }

                     return vbox({
                         filler(),
                         paragraphAlignCenter(STR_INCREASE_SIZE) | bold |
                         size(WIDTH, LESS_THAN, 60) | hcenter,
                         paragraphAlignCenter(STR_VIEWPORT_PART1 +
                                              std::to_string(this->panes.size()) +
                                              STR_VIEWPORT_PART2) |
                         size(WIDTH, LESS_THAN, 60) | hcenter,
                         text(""),
                         text(STR_STATUS) | hcenter,
                         std::move(warning_status),
                         text(""),
                         resize_exit->Render() | hcenter,
                         filler(),
                       }) | flex;
                   }

                   int heading_text_width =
                     std::max(1, this->app.dimx() - 2 - top_controls_width);
                   std::string version_text = "Adaptyst " + this->version;
                   Element version = string_width(version_text) <= heading_text_width
                     ? hbox({text("Adaptyst ") | bold, text(this->version)})
                     : text("Adaptyst") | bold;
                   std::string copyright_text = STR_COPYRIGHT;

                   if (string_width(copyright_text) > heading_text_width) {
                     copyright_text = STR_COPYRIGHT_SHORT;
                   }

                   Element heading = hbox({
                       vbox({
                           std::move(version),
                           text(copyright_text) | dim,
                         }) | flex,
                       vbox({add->Render() | align_right,
                           exit->Render() | align_right}) | align_right,
                     }) | border;

                   bool capacity_warning = this->capacity_notice_ticks > 0;
                   std::string footer_text = this->status;

                   if (capacity_warning) {
                     footer_text = STR_INCREASE_SIZE_LONG;
                   }

                   footer_text = ellipsize(std::move(footer_text),
                                           std::max(1, this->app.dimx() - 2));
                   Element footer = text(footer_text);

                   if (!capacity_warning &&
                       this->finished.load(std::memory_order_acquire) && this->formatted) {
                     footer = std::move(footer) |
                       color(this->successful ? Color::Green : Color::Red) | bold;
                   }

                   return vbox({
                       std::move(heading),
                       this->tile_component->Render() | flex | reflect(this->tile_box),
                       std::move(footer) | border,
                     });
                 });

      auto picker_menu_options = MenuOption::Vertical();
      picker_menu_options.on_enter = [this] { this->choose_source(); };

      auto picker_menu = Menu(&this->source_labels, &this->picker_selected,
                              std::move(picker_menu_options));
      this->picker_menu_component = picker_menu;

      auto picker = Renderer(picker_menu, [this, picker_menu] {
        return vbox({
            text(STR_OUTPUT_SELECT) | bold,
            separator(),
            picker_menu->Render() | reflect(this->picker_menu_box) | yframe |
            size(HEIGHT, LESS_THAN, 15) | reflect(this->picker_view_box),
          }) | size(WIDTH, GREATER_THAN, 30) |
          size(WIDTH, LESS_THAN, 70) | border | clear_under;
      });

      picker = CatchEvent(picker, [this](Event event) {
        if (event == Event::Escape) {
          this->picker_shown = false;
          return true;
        }

        if (event.is_mouse() && event.mouse().button == Mouse::Left &&
            event.mouse().motion == Mouse::Pressed &&
            this->picker_view_box.Contain(event.mouse().x, event.mouse().y)) {
          int selected = event.mouse().y - this->picker_menu_box.y_min;

          if (selected >= 0 &&
              selected < static_cast<int>(this->picker_sources.size())) {
            this->picker_selected = selected;
            this->choose_source();
            return true;
          }
        }

        return false;
      });

      main_renderer |= Modal(picker, &this->picker_shown);

      auto confirm = Button(STR_YES, [this] { this->exit_immediately(); },
                            ButtonOption::Ascii());
      auto keep_running = Button(STR_NO, [this] {
        this->confirmation_shown = false;
      }, ButtonOption::Ascii());
      auto confirm_controls = Container::Horizontal({confirm, keep_running});
      auto confirmation = Renderer(confirm_controls,
                                   [confirm, keep_running] {
                                     return vbox({
                                         text(STR_ANALYSIS_RUNNING) | bold,
                                         text(STR_EXIT_CONFIRMATION),
                                         separator(),
                                         hbox({confirm->Render(), text(" "), keep_running->Render()}),
                                       }) | border | clear_under;
                                   });

      confirmation = CatchEvent(confirmation, [this](Event event) {
        if (event == Event::Escape) {
          this->confirmation_shown = false;
          return true;
        }

        return false;
      });
      main_renderer |= Modal(confirmation, &this->confirmation_shown);
      main_renderer = CatchEvent(main_renderer, [this](Event event) {
        if (event == Event::Custom) {
          this->refresh();
          return true;
        }

        if (event == Event::CtrlC || event == Event::CtrlQ) {
          this->request_exit();
          return true;
        }

        if (this->viewport_too_small) {
          return false;
        }

        if (event == Event::Tab && this->exit_button->Focused()) {
          auto &pane = this->panes.front();
          pane->selected_control = 0;
          pane->controls->TakeFocus();
          return true;
        }

        if (event == Event::TabReverse && this->add_button->Focused()) {
          auto &pane = this->panes.back();
          pane->selected_control = pane->close_control_index;
          pane->controls->TakeFocus();
          return true;
        }

        return false;
      });

      return main_renderer;
    }

  public:
    Impl(Terminal &terminal, std::string version, int log_source_fd,
         unsigned int buf_size, unsigned int max_read_size,
         int max_lines)
      : terminal(terminal), formatted(terminal.is_formatted()),
        version(std::move(version)),
        main_log_path(fs::path(terminal.get_log_dir()) / "Adaptyst.log"),
        max_read_size(max_read_size),
        max_lines(max_lines),
        tile_component(Make<DynamicComponent>()) {
      if (log_source_fd != -1) {
        int read_fd[2] = {log_source_fd, -1};
        this->log_source_fd =
          std::make_unique<FileDescriptor>(read_fd, nullptr, buf_size);
      }

      this->sources = {{"Main", this->main_log_path}};
      this->source_labels = {"Main"};
      this->add_pane(this->sources.front());
      this->root = this->make_root();
    }

    bool run() {
      this->app.ForceHandleCtrlC(false);
      this->refresh();

      std::jthread ticker([this](std::stop_token stop) {
        while (!stop.stop_requested()) {
          std::this_thread::sleep_for(std::chrono::milliseconds(200));
          this->app.PostEvent(Event::Custom);
        }
      });

      this->app.Loop(this->root);
      ticker.request_stop();

      if (!this->finished.load(std::memory_order_acquire)) {
        this->aborted = true;
      }

      return this->aborted;
    }

    void finish(bool successful) {
      this->successful = successful;
      this->finished.store(true, std::memory_order_release);
      this->app.PostEvent(Event::Custom);
    }
  };

  Tui::Tui(Terminal &terminal, std::string version, int log_source_fd,
           unsigned int buf_size, unsigned int max_read_size,
           int max_lines)
    : impl(std::make_unique<Impl>(terminal, std::move(version),
                                  log_source_fd, buf_size,
                                  max_read_size, max_lines)) {}

  Tui::~Tui() = default;

  bool Tui::run() {
    return this->impl->run();
  }

  void Tui::finish(bool successful) {
    this->impl->finish(successful);
  }
};
