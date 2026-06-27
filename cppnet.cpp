// =============================================================================
//  pdf_generator.cpp
//  A self-contained, feature-rich PDF report generator in plain C++17.
//
//  Features:
//   - Bug-fixed from original (tolower on string, UTF-8 ellipsis in PDF)
//   - Configuration struct (colors, margins, watermark, company branding)
//   - Cover page with title, subtitle, company name, date range
//   - Table of Contents page
//   - Multi-section reports (multiple titled tables in one PDF)
//   - Auto-fit column widths based on content
//   - Numeric column detection with right-alignment
//   - Alternating row shading
//   - Color-coded status cells (Active, Pending, Complete, etc.)
//   - Summary statistics row (Total, Average, Min, Max for numeric columns)
//   - Horizontal bar chart rendered in PDF (from a numeric column)
//   - Page watermark (CONFIDENTIAL, DRAFT, etc.)
//   - Page border decoration
//   - Themed header colors per report type
//   - Custom footer (company name + confidentiality notice)
//   - CSV string parser → tableData
//   - Number formatter (commas + optional currency symbol)
//   - Pagination across all sections
//   - Comprehensive demo in main()
// =============================================================================

#include <iostream>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <vector>
#include <string>
#include <algorithm>
#include <ctime>
#include <cctype>
#include <cmath>
#include <map>
#include <set>
#include <numeric>
#include <stdexcept>

// =============================================================================
//  SECTION 1: Utility Functions
// =============================================================================

namespace PDFUtil {

/// Trim whitespace from both ends of a string.
std::string trim(const std::string& s) {
    std::size_t b = 0, e = s.size();
    while (b < e && std::isspace(static_cast<unsigned char>(s[b]))) ++b;
    while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) --e;
    return s.substr(b, e - b);
}

/// Convert a string to lowercase in-place using std::transform.
/// BUG FIX: original code called tolower(std::string) which does not compile.
std::string toLower(const std::string& s) {
    std::string result = s;
    std::transform(result.begin(), result.end(), result.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return result;
}

/// Capitalize the first character of a string.
std::string capitalize(const std::string& s) {
    if (s.empty()) return s;
    std::string r = s;
    r[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(r[0])));
    return r;
}

/// Escape characters that are special inside PDF string literals: ( ) and backslash
/// BUG FIX: replaces the original UTF-8 "..." ellipsis with ASCII "..."
/// for truncation, ensuring stream byte-length declarations stay accurate.
std::string escapePDF(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 8);
    for (char c : s) {
        if (c == '(' || c == ')' || c == '\\')
            out.push_back('\\');
        out.push_back(c);
    }
    return out;
}

/// Truncate a cell string to maxLen chars.
/// BUG FIX: uses ASCII "..." instead of UTF-8 ellipsis to avoid
/// corrupting PDF stream byte counts and Helvetica WinAnsiEncoding.
std::string truncateCell(const std::string& s, std::size_t maxLen = 40) {
    if (s.size() <= maxLen) return s;
    return s.substr(0, maxLen - 3) + "...";
}

/// Return true if s looks like a numeric value
/// (integers, decimals, thousands separators, leading sign, trailing %).
bool isNumericStr(const std::string& raw) {
    std::string s = trim(raw);
    // Strip currency prefix ($ £ € ₹)
    if (!s.empty() && (s[0] == '$' || s[0] == '\xa3' || s[0] == '\x80' || s[0] == '\xa5'))
        s = s.substr(1);
    if (s.empty()) return false;
    std::size_t i = 0;
    bool hasDigits = false, hasDot = false;
    if (i < s.size() && (s[i] == '+' || s[i] == '-')) ++i;
    for (; i < s.size(); ++i) {
        char c = s[i];
        if (std::isdigit(static_cast<unsigned char>(c))) { hasDigits = true; continue; }
        if (c == ',')  continue;                          // thousands separator
        if (c == '.' && !hasDot) { hasDot = true; continue; }
        if (c == '%' && i == s.size() - 1) break;        // trailing percent
        return false;
    }
    return hasDigits;
}

/// Parse a numeric string into double (strips commas, $, %).
double parseNumeric(const std::string& raw) {
    std::string s = trim(raw);
    if (!s.empty() && (s[0] == '$' || s[0] == '\xa3' || s[0] == '\xa5'))
        s = s.substr(1);
    if (!s.empty() && s.back() == '%')
        s.pop_back();
    s.erase(std::remove(s.begin(), s.end(), ','), s.end());
    try { return std::stod(s); }
    catch (...) { return 0.0; }
}

/// Format a double as a string with comma-separated thousands and n decimal places.
std::string formatNumber(double v, int decimals = 2, const std::string& prefix = "") {
    bool negative = v < 0;
    if (negative) v = -v;
    std::ostringstream tmp;
    tmp << std::fixed << std::setprecision(decimals) << v;
    std::string s = tmp.str();
    // Insert commas
    std::size_t dot = s.find('.');
    std::size_t intEnd = (dot == std::string::npos) ? s.size() : dot;
    for (int pos = static_cast<int>(intEnd) - 3; pos > 0; pos -= 3)
        s.insert(static_cast<std::size_t>(pos), ",");
    return (negative ? "-" : "") + prefix + s;
}

/// Format a timestamp as "YYYY-MM-DD HH:MM:SS".
std::string formatTimestamp(const std::tm& tm) {
    std::ostringstream ss;
    ss << std::put_time(&tm, "%Y-%m-%d %H:%M:%S");
    return ss.str();
}

/// Format timestamp as PDF date string "D:YYYYMMDDHHmmSS".
std::string formatPDFDate(const std::tm& tm) {
    char buf[32];
    std::strftime(buf, sizeof(buf), "D:%Y%m%d%H%M%S", &tm);
    return std::string(buf);
}

/// Parse a CSV-formatted string into a 2D table (vector of rows).
/// Handles quoted fields and embedded commas inside quotes.
std::vector<std::vector<std::string>> parseCSV(const std::string& csv) {
    std::vector<std::vector<std::string>> result;
    std::istringstream stream(csv);
    std::string line;
    while (std::getline(stream, line)) {
        if (trim(line).empty()) continue;
        std::vector<std::string> row;
        bool inQuotes = false;
        std::string field;
        for (std::size_t i = 0; i < line.size(); ++i) {
            char c = line[i];
            if (c == '"') {
                if (inQuotes && i + 1 < line.size() && line[i + 1] == '"') {
                    field += '"'; ++i;  // escaped quote inside quotes
                } else {
                    inQuotes = !inQuotes;
                }
            } else if (c == ',' && !inQuotes) {
                row.push_back(trim(field));
                field.clear();
            } else {
                field += c;
            }
        }
        row.push_back(trim(field));
        result.push_back(row);
    }
    return result;
}

} // namespace PDFUtil

// =============================================================================
//  SECTION 2: Configuration Struct
// =============================================================================

/// RGB color, each channel 0.0–1.0.
struct PDFColor {
    double r, g, b;
    PDFColor(double r = 0.0, double g = 0.0, double b = 0.0) : r(r), g(g), b(b) {}

    std::string fillOp()   const {
        std::ostringstream ss;
        ss << std::fixed << std::setprecision(3) << r << " " << g << " " << b << " rg";
        return ss.str();
    }
    std::string strokeOp() const {
        std::ostringstream ss;
        ss << std::fixed << std::setprecision(3) << r << " " << g << " " << b << " RG";
        return ss.str();
    }
};

// Predefined palette
namespace Colors {
    const PDFColor Black      {0.00, 0.00, 0.00};
    const PDFColor White      {1.00, 1.00, 1.00};
    const PDFColor LightGray  {0.93, 0.93, 0.93};
    const PDFColor MidGray    {0.65, 0.65, 0.65};
    const PDFColor DarkGray   {0.25, 0.25, 0.25};
    const PDFColor NavyBlue   {0.10, 0.20, 0.50};
    const PDFColor SteelBlue  {0.27, 0.51, 0.71};
    const PDFColor LightBlue  {0.87, 0.92, 0.97};
    const PDFColor DarkRed    {0.60, 0.10, 0.10};
    const PDFColor LightRed   {0.98, 0.90, 0.90};
    const PDFColor DarkGreen  {0.10, 0.45, 0.15};
    const PDFColor LightGreen {0.88, 0.97, 0.88};
    const PDFColor Orange     {0.90, 0.50, 0.05};
    const PDFColor LightOrange{0.99, 0.93, 0.82};
    const PDFColor Gold       {0.80, 0.65, 0.00};
    const PDFColor Purple     {0.35, 0.10, 0.55};
    const PDFColor Teal       {0.10, 0.50, 0.50};
}

/// Watermark style
enum class WatermarkStyle { None, Confidential, Draft, TopSecret, Copy };

/// Report theme — controls the color scheme of headers/accents
enum class ReportTheme { Default, Sales, Finance, HR, Technical, Executive };

/// Configuration for the entire PDF document.
struct PDFConfig {
    // Company branding
    std::string companyName     = "My Company";
    std::string reportTitle     = "Project Report";
    std::string reportSubtitle  = "";
    std::string authorName      = "Report Generator";
    std::string footerNote      = "CONFIDENTIAL — For internal use only.";
    std::string currencySymbol  = "$";

    // Date range
    std::string fromDate = "";
    std::string toDate   = "";

    // Geometry
    double pageWidth   = 612.0;   // US Letter
    double pageHeight  = 792.0;
    double marginLeft  = 72.0;
    double marginRight = 72.0;
    double marginTop   = 72.0;
    double marginBottom= 72.0;
    double rowHeight   = 18.0;

    // Features
    bool showCoverPage    = true;
    bool showTOC          = true;
    bool showSummaryStats = true;
    bool showBarChart     = true;
    bool showPageBorder   = true;
    bool alternateRows    = true;
    int  maxColumns       = 7;

    // Watermark
    WatermarkStyle watermark = WatermarkStyle::None;

    // Theme
    ReportTheme theme = ReportTheme::Default;

    // Status-cell color map (cell text → background color)
    std::map<std::string, PDFColor> statusColors = {
        {"active",    Colors::LightGreen},
        {"complete",  Colors::LightBlue},
        {"completed", Colors::LightBlue},
        {"done",      Colors::LightBlue},
        {"pending",   Colors::LightOrange},
        {"in progress", Colors::LightOrange},
        {"failed",    Colors::LightRed},
        {"error",     Colors::LightRed},
        {"cancelled", Colors::LightRed},
        {"hold",      Colors::LightGray},
        {"on hold",   Colors::LightGray},
    };

    /// Return the header fill color for this theme.
    PDFColor headerFill() const {
        switch (theme) {
            case ReportTheme::Sales:     return Colors::NavyBlue;
            case ReportTheme::Finance:   return Colors::DarkGreen;
            case ReportTheme::HR:        return Colors::Purple;
            case ReportTheme::Technical: return Colors::Teal;
            case ReportTheme::Executive: return Colors::DarkGray;
            default:                     return Colors::SteelBlue;
        }
    }

    /// Return the header text color.
    PDFColor headerText() const { return Colors::White; }

    /// Return the accent color used for chart bars, TOC lines, etc.
    PDFColor accent() const {
        switch (theme) {
            case ReportTheme::Sales:     return Colors::NavyBlue;
            case ReportTheme::Finance:   return Colors::DarkGreen;
            case ReportTheme::HR:        return Colors::Purple;
            case ReportTheme::Technical: return Colors::Teal;
            case ReportTheme::Executive: return Colors::DarkGray;
            default:                     return Colors::SteelBlue;
        }
    }
};

// =============================================================================
//  SECTION 3: Report Section Definition
// =============================================================================

/// A single section of the report: a title + table data + optional chart column.
struct ReportSection {
    std::string title;                          ///< Section title shown above the table
    std::string description;                    ///< Optional short description below the title
    std::vector<std::vector<std::string>> rows; ///< rows[0] = headers; rows[1..] = data
    int  chartColumnIndex = -1;                 ///< Column index to draw bar chart for (-1 = none)
    bool showStats        = true;               ///< Whether to show summary stats for this section
};

// =============================================================================
//  SECTION 4: PDF Low-Level Stream Builder
// =============================================================================

/// Wraps a std::ostringstream and provides helpers for common PDF operators.
class PDFStream {
public:
    void setFillColor(const PDFColor& c) {
        ss_ << c.fillOp() << "\n";
    }
    void setStrokeColor(const PDFColor& c) {
        ss_ << c.strokeOp() << "\n";
    }
    void setLineWidth(double w) {
        ss_ << std::fixed << std::setprecision(2) << w << " w\n";
    }
    void rect(double x, double y, double w, double h, bool fill, bool stroke) {
        ss_ << std::fixed << std::setprecision(2)
            << x << " " << y << " " << w << " " << h << " re ";
        if (fill && stroke) ss_ << "B\n";
        else if (fill)      ss_ << "f\n";
        else if (stroke)    ss_ << "S\n";
        else                ss_ << "n\n";
    }
    void line(double x1, double y1, double x2, double y2) {
        ss_ << std::fixed << std::setprecision(2)
            << x1 << " " << y1 << " m " << x2 << " " << y2 << " l S\n";
    }
    void text(double x, double y, const std::string& font, double size,
              const std::string& str) {
        ss_ << "BT\n/" << font << " " << std::fixed << std::setprecision(2)
            << size << " Tf\n"
            << x << " " << y << " Td\n"
            << "(" << PDFUtil::escapePDF(str) << ") Tj\nET\n";
    }
    /// Render text rotated 45 degrees (for watermarks).
    void textRotated(double x, double y, const std::string& font, double size,
                     const std::string& str, double angleDeg) {
        double rad = angleDeg * 3.14159265358979 / 180.0;
        double cosA = std::cos(rad), sinA = std::sin(rad);
        ss_ << "BT\n/" << font << " " << std::fixed << std::setprecision(3)
            << size << " Tf\n"
            << cosA << " " << sinA << " " << -sinA << " " << cosA
            << " " << x << " " << y << " Tm\n"
            << "(" << PDFUtil::escapePDF(str) << ") Tj\nET\n";
    }
    std::string str() const { return ss_.str(); }
    void reset() { ss_.str(""); ss_.clear(); }

private:
    std::ostringstream ss_;
};

// =============================================================================
//  SECTION 5: Column Layout Calculator
// =============================================================================

struct ColumnLayout {
    std::size_t        numCols   = 0;
    std::vector<double> widths;
    std::vector<bool>  isNumeric;
    double             totalWidth = 0.0;

    /// Build layout from table data and config constraints.
    void compute(const std::vector<std::vector<std::string>>& rows,
                 double totalW, int maxCols,
                 double avgCharW = 5.5, double cellPad = 6.0, double minColW = 36.0)
    {
        totalWidth = totalW;
        numCols = 0;
        for (const auto& r : rows) numCols = std::max(numCols, r.size());
        if (numCols == 0) numCols = 1;
        if (numCols > static_cast<std::size_t>(maxCols))
            numCols = static_cast<std::size_t>(maxCols);

        // Measure max character length per column (capped to avoid extremes)
        std::vector<std::size_t> maxChars(numCols, 6);
        for (const auto& row : rows) {
            for (std::size_t c = 0; c < numCols && c < row.size(); ++c) {
                std::size_t len = std::min(row[c].size(), std::size_t(30));
                maxChars[c] = std::max(maxChars[c], len);
            }
        }

        // Desired width proportional to content length
        widths.resize(numCols);
        double desiredSum = 0.0;
        for (std::size_t c = 0; c < numCols; ++c) {
            widths[c] = std::max(minColW, 2.0 * cellPad + avgCharW * static_cast<double>(maxChars[c]));
            desiredSum += widths[c];
        }
        // Scale to fit total width
        if (desiredSum > 0.0) {
            double scale = totalW / desiredSum;
            for (auto& w : widths) w *= scale;
            // Correct floating-point drift in last column
            double sumFirst = 0.0;
            for (std::size_t c = 0; c + 1 < numCols; ++c) sumFirst += widths[c];
            widths[numCols - 1] = std::max(20.0, totalW - sumFirst);
        }

        // Detect which columns are numeric (ignore header row at index 0)
        isNumeric.assign(numCols, rows.size() > 1);
        for (std::size_t r = 1; r < rows.size(); ++r) {
            for (std::size_t c = 0; c < numCols; ++c) {
                if (c < rows[r].size()) {
                    std::string cell = PDFUtil::trim(rows[r][c]);
                    if (!cell.empty() && !PDFUtil::isNumericStr(cell))
                        isNumeric[c] = false;
                }
            }
        }
    }

    /// X coordinate of the left edge of column c.
    double colX(double tableLeft, std::size_t c) const {
        double x = tableLeft;
        for (std::size_t i = 0; i < c && i < widths.size(); ++i) x += widths[i];
        return x;
    }
};

// =============================================================================
//  SECTION 6: Statistics Calculator
// =============================================================================

struct ColumnStats {
    bool   valid  = false;
    double total  = 0.0;
    double minVal = 0.0;
    double maxVal = 0.0;
    double avg    = 0.0;
    std::size_t count = 0;
};

/// Compute per-column statistics from data rows (skips header at index 0).
std::vector<ColumnStats> computeStats(
    const std::vector<std::vector<std::string>>& rows,
    const std::vector<bool>& isNumeric,
    std::size_t numCols)
{
    std::vector<ColumnStats> stats(numCols);
    for (std::size_t c = 0; c < numCols; ++c) {
        if (!isNumeric[c]) continue;
        stats[c].valid = true;
        bool first = true;
        for (std::size_t r = 1; r < rows.size(); ++r) {
            if (c >= rows[r].size()) continue;
            std::string cell = PDFUtil::trim(rows[r][c]);
            if (cell.empty()) continue;
            double v = PDFUtil::parseNumeric(cell);
            stats[c].total += v;
            stats[c].count++;
            if (first) { stats[c].minVal = stats[c].maxVal = v; first = false; }
            else {
                stats[c].minVal = std::min(stats[c].minVal, v);
                stats[c].maxVal = std::max(stats[c].maxVal, v);
            }
        }
        if (stats[c].count > 0)
            stats[c].avg = stats[c].total / static_cast<double>(stats[c].count);
    }
    return stats;
}

// =============================================================================
//  SECTION 7: Page Renderer
// =============================================================================

/// Holds all PDF object streams that will be assembled into the final PDF.
class PageRenderer {
public:
    explicit PageRenderer(const PDFConfig& cfg) : cfg_(cfg) {
        contentArea_ = cfg.pageWidth - cfg.marginLeft - cfg.marginRight;
    }

    // ── Cover Page ────────────────────────────────────────────────────────────
    std::string renderCoverPage(const std::string& timestamp, const std::string& dateRange,
                                const std::vector<ReportSection>& sections) const
    {
        PDFStream s;
        double cy = cfg_.pageHeight / 2.0;

        // Background accent strip at top
        s.setFillColor(cfg_.accent());
        s.rect(0, cfg_.pageHeight - 140, cfg_.pageWidth, 140, true, false);

        // White title text in the accent strip
        s.setFillColor(Colors::White);
        s.text(cfg_.marginLeft, cfg_.pageHeight - 70, "F2", 28,
               cfg_.reportTitle);
        if (!cfg_.reportSubtitle.empty())
            s.text(cfg_.marginLeft, cfg_.pageHeight - 100, "F1", 14,
                   cfg_.reportSubtitle);

        // Thin accent line below the strip
        s.setStrokeColor(cfg_.accent());
        s.setLineWidth(2.0);
        s.line(cfg_.marginLeft, cfg_.pageHeight - 155,
               cfg_.pageWidth - cfg_.marginRight, cfg_.pageHeight - 155);

        // Company name
        s.setFillColor(Colors::DarkGray);
        s.text(cfg_.marginLeft, cfg_.pageHeight - 200, "F2", 18,
               cfg_.companyName);

        // Date range block
        s.setFillColor(Colors::MidGray);
        s.text(cfg_.marginLeft, cfg_.pageHeight - 240, "F1", 11, dateRange);
        s.text(cfg_.marginLeft, cfg_.pageHeight - 260, "F1", 11,
               "Generated: " + timestamp);
        s.text(cfg_.marginLeft, cfg_.pageHeight - 280, "F1", 11,
               "Author: " + cfg_.authorName);

        // Section list preview
        s.setFillColor(cfg_.accent());
        s.text(cfg_.marginLeft, cy + 20, "F2", 12, "Report Contents");
        s.setLineWidth(0.5);
        s.setStrokeColor(cfg_.accent());
        s.line(cfg_.marginLeft, cy + 14,
               cfg_.pageWidth - cfg_.marginRight, cy + 14);

        s.setFillColor(Colors::DarkGray);
        double sy = cy - 4;
        for (std::size_t i = 0; i < sections.size() && i < 12; ++i) {
            s.text(cfg_.marginLeft + 10, sy, "F1", 11,
                   std::to_string(i + 1) + ".  " + sections[i].title);
            sy -= 20;
        }

        // Bottom tagline / confidentiality note
        s.setFillColor(Colors::MidGray);
        s.text(cfg_.marginLeft, cfg_.marginBottom + 20, "F1", 9, cfg_.footerNote);

        // Page border
        if (cfg_.showPageBorder) renderPageBorder(s);

        // Watermark
        renderWatermark(s);

        return s.str();
    }

    // ── Table of Contents Page ────────────────────────────────────────────────
    std::string renderTOCPage(const std::vector<ReportSection>& sections,
                              const std::vector<int>& sectionPageNums,
                              const std::string& timestamp) const
    {
        PDFStream s;
        // Title bar
        s.setFillColor(cfg_.accent());
        s.rect(cfg_.marginLeft, cfg_.pageHeight - 100,
               contentArea_, 36, true, false);
        s.setFillColor(Colors::White);
        s.text(cfg_.marginLeft + 10, cfg_.pageHeight - 78, "F2", 16,
               "Table of Contents");

        // Divider
        s.setStrokeColor(cfg_.accent());
        s.setLineWidth(1.0);
        s.line(cfg_.marginLeft, cfg_.pageHeight - 110,
               cfg_.pageWidth - cfg_.marginRight, cfg_.pageHeight - 110);

        double y = cfg_.pageHeight - 140;
        for (std::size_t i = 0; i < sections.size(); ++i) {
            // Alternating row bg
            if (i % 2 == 0) {
                s.setFillColor(Colors::LightGray);
                s.rect(cfg_.marginLeft, y - 4, contentArea_, 20, true, false);
            }
            s.setFillColor(Colors::DarkGray);
            // Section number and title
            std::string label = std::to_string(i + 1) + ".   " + sections[i].title;
            s.text(cfg_.marginLeft + 8, y + 2, "F1", 11, label);

            // Dot leader + page number
            std::string pageStr = "Page " + std::to_string(sectionPageNums[i]);
            s.text(cfg_.pageWidth - cfg_.marginRight - 55, y + 2, "F1", 11, pageStr);

            // Dot leader line
            s.setStrokeColor(Colors::MidGray);
            s.setLineWidth(0.3);
            double dotStart = cfg_.marginLeft + 8 + 6.0 * static_cast<double>(label.size());
            double dotEnd   = cfg_.pageWidth - cfg_.marginRight - 60;
            if (dotEnd > dotStart + 20) {
                // Draw dots as a dashed line
                for (double dx = dotStart + 6; dx < dotEnd - 4; dx += 6) {
                    s.line(dx, y + 6, dx + 2, y + 6);
                }
            }
            y -= 24;
        }

        renderFooter(s, 2, 0, timestamp); // TOC is page 2
        if (cfg_.showPageBorder) renderPageBorder(s);
        renderWatermark(s);
        return s.str();
    }

    // ── Table Page ────────────────────────────────────────────────────────────
    /// Render one page worth of table rows for a section.
    std::string renderTablePage(
        const ReportSection& section,
        const ColumnLayout& layout,
        std::size_t startDataRow,
        std::size_t dataCount,
        bool isFirstPageOfSection,
        const std::vector<ColumnStats>& stats,
        bool isLastPageOfSection,
        int pageNum, int totalPages,
        const std::string& timestamp) const
    {
        PDFStream s;

        double left  = cfg_.marginLeft;
        double top   = cfg_.pageHeight - cfg_.marginTop;

        // Section title banner on first page of section
        if (isFirstPageOfSection) {
            s.setFillColor(cfg_.accent());
            s.rect(left, top - 30, contentArea_, 26, true, false);
            s.setFillColor(Colors::White);
            s.text(left + 8, top - 20, "F2", 13, section.title);
            top -= 36;

            // Optional section description
            if (!section.description.empty()) {
                s.setFillColor(Colors::DarkGray);
                s.text(left, top - 12, "F1", 10, section.description);
                top -= 20;
            }
            top -= 4;
        }

        // Table header row
        s.setFillColor(cfg_.headerFill());
        s.rect(left, top - cfg_.rowHeight, contentArea_, cfg_.rowHeight, true, false);

        // Header text
        if (!section.rows.empty()) {
            const auto& headerRow = section.rows[0];
            for (std::size_t c = 0; c < layout.numCols; ++c) {
                std::string cell = c < headerRow.size() ? headerRow[c] : "";
                cell = PDFUtil::truncateCell(cell, 28);
                double tx = layout.colX(left, c) + 3.0;
                double ty = top - cfg_.rowHeight + 5.0;
                s.setFillColor(cfg_.headerText());
                s.text(tx, ty, "F2", 9, cell);
            }
        }

        // Grid: horizontal lines
        s.setStrokeColor(Colors::MidGray);
        s.setLineWidth(0.4);
        for (std::size_t r = 0; r <= dataCount + 1; ++r) {
            double y = top - static_cast<double>(r) * cfg_.rowHeight;
            s.line(left, y, left + contentArea_, y);
        }
        // Grid: vertical lines
        double xv = left;
        for (std::size_t c = 0; c <= layout.numCols; ++c) {
            s.line(xv, top, xv, top - (dataCount + 1) * cfg_.rowHeight);
            if (c < layout.numCols) xv += layout.widths[c];
        }

        // Data rows
        const auto& rows = section.rows;
        for (std::size_t r = 0; r < dataCount; ++r) {
            std::size_t rowIdx = startDataRow + r;
            if (rowIdx >= rows.size()) break;
            const auto& row = rows[rowIdx];

            double rowTop = top - (r + 1) * cfg_.rowHeight;

            // Alternating row background
            if (cfg_.alternateRows && r % 2 == 1) {
                s.setFillColor(Colors::LightGray);
                s.rect(left, rowTop, contentArea_, cfg_.rowHeight, true, false);
            }

            for (std::size_t c = 0; c < layout.numCols; ++c) {
                std::string cell = c < row.size() ? PDFUtil::trim(row[c]) : "";
                cell = PDFUtil::truncateCell(cell, 32);

                // Status cell coloring
                std::string lower = PDFUtil::toLower(cell);
                auto it = cfg_.statusColors.find(lower);
                if (it != cfg_.statusColors.end()) {
                    double cx2 = layout.colX(left, c);
                    s.setFillColor(it->second);
                    s.rect(cx2 + 1, rowTop + 1,
                           layout.widths[c] - 2, cfg_.rowHeight - 2, true, false);
                }

                // Text positioning: right-align numeric cols
                double ty = rowTop + 4.0;
                double tx;
                const double avgCharW = 5.0;
                if (layout.isNumeric[c]) {
                    double textW = static_cast<double>(cell.size()) * avgCharW;
                    double colEnd = layout.colX(left, c) + layout.widths[c];
                    tx = colEnd - 3.0 - textW;
                    tx = std::max(tx, layout.colX(left, c) + 2.0);
                } else {
                    tx = layout.colX(left, c) + 3.0;
                }
                s.setFillColor(Colors::Black);
                s.text(tx, ty, "F1", 9, cell);
            }
        }

        // Summary stats row (shown on last page of section)
        if (cfg_.showSummaryStats && isLastPageOfSection && !stats.empty()) {
            double statsTop = top - (dataCount + 1) * cfg_.rowHeight;
            // Stats header label background
            s.setFillColor(cfg_.accent());
            s.rect(left, statsTop - cfg_.rowHeight * 3,
                   contentArea_, cfg_.rowHeight * 3, true, false);

            // Label column
            s.setFillColor(Colors::White);
            s.text(left + 4, statsTop - cfg_.rowHeight     + 5, "F2", 8, "TOTAL");
            s.text(left + 4, statsTop - cfg_.rowHeight * 2 + 5, "F2", 8, "AVERAGE");
            s.text(left + 4, statsTop - cfg_.rowHeight * 3 + 5, "F2", 8, "MIN / MAX");

            // Per-column stats
            for (std::size_t c = 0; c < layout.numCols; ++c) {
                if (c >= stats.size() || !stats[c].valid) continue;
                double colEnd = layout.colX(left, c) + layout.widths[c];
                const double avgW = 5.0;

                auto place = [&](double rowOffset, const std::string& val) {
                    double tw = static_cast<double>(val.size()) * avgW;
                    double tx2 = colEnd - 3.0 - tw;
                    tx2 = std::max(tx2, layout.colX(left, c) + 2.0);
                    s.text(tx2, statsTop - rowOffset * cfg_.rowHeight + 5, "F2", 8, val);
                };

                std::string totalStr = PDFUtil::formatNumber(stats[c].total, 2, cfg_.currencySymbol);
                std::string avgStr   = PDFUtil::formatNumber(stats[c].avg,   2, cfg_.currencySymbol);
                std::string rangeStr = PDFUtil::formatNumber(stats[c].minVal, 0) + " / "
                                     + PDFUtil::formatNumber(stats[c].maxVal, 0);
                place(1, totalStr);
                place(2, avgStr);
                place(3, rangeStr);
            }
            // Border around stats block
            s.setStrokeColor(cfg_.accent());
            s.setLineWidth(0.8);
            s.rect(left, statsTop - cfg_.rowHeight * 3,
                   contentArea_, cfg_.rowHeight * 3, false, true);
        }

        renderFooter(s, pageNum, totalPages, timestamp);
        if (cfg_.showPageBorder) renderPageBorder(s);
        renderWatermark(s);
        return s.str();
    }

    // ── Bar Chart Page ────────────────────────────────────────────────────────
    /// Render a simple horizontal bar chart for one numeric column.
    std::string renderBarChartPage(
        const ReportSection& section,
        std::size_t chartCol,
        const ColumnLayout& /*layout*/,
        int pageNum, int totalPages,
        const std::string& timestamp) const
    {
        PDFStream s;
        double left   = cfg_.marginLeft;
        double top    = cfg_.pageHeight - cfg_.marginTop;
        double width  = contentArea_;

        // Chart title bar
        s.setFillColor(cfg_.accent());
        s.rect(left, top - 30, width, 26, true, false);
        s.setFillColor(Colors::White);
        std::string headerName = (chartCol < section.rows[0].size())
                                  ? section.rows[0][chartCol] : "Value";
        s.text(left + 10, top - 20, "F2", 12,
               section.title + " — " + headerName + " Chart");

        // Collect label + value pairs
        struct Bar { std::string label; double value; };
        std::vector<Bar> bars;
        double maxVal = 0.0;
        const std::size_t labelCol = (chartCol == 0) ? 1 : 0;

        for (std::size_t r = 1; r < section.rows.size(); ++r) {
            const auto& row = section.rows[r];
            std::string lbl = (labelCol < row.size()) ? PDFUtil::trim(row[labelCol]) : "Row " + std::to_string(r);
            std::string val = (chartCol < row.size()) ? PDFUtil::trim(row[chartCol]) : "0";
            if (!PDFUtil::isNumericStr(val)) continue;
            double v = PDFUtil::parseNumeric(val);
            bars.push_back({PDFUtil::truncateCell(lbl, 22), v});
            maxVal = std::max(maxVal, v);
        }

        // Cap to 20 bars per chart page
        if (bars.size() > 20) bars.resize(20);

        double chartTop    = top - 46;
        double barHeight   = 22.0;
        double barGap      = 8.0;
        double labelWidth  = 120.0;
        double barAreaLeft = left + labelWidth + 10;
        double barAreaW    = width - labelWidth - 60;
        double valueColX   = barAreaLeft + barAreaW + 6;

        // Axis
        s.setStrokeColor(Colors::MidGray);
        s.setLineWidth(0.5);
        s.line(barAreaLeft, chartTop,
               barAreaLeft, chartTop - static_cast<double>(bars.size()) * (barHeight + barGap) - 10);
        s.line(barAreaLeft, chartTop - static_cast<double>(bars.size()) * (barHeight + barGap) - 10,
               barAreaLeft + barAreaW, chartTop - static_cast<double>(bars.size()) * (barHeight + barGap) - 10);

        // Bars
        for (std::size_t i = 0; i < bars.size(); ++i) {
            double by = chartTop - static_cast<double>(i) * (barHeight + barGap) - barHeight;
            double barW = (maxVal > 0) ? (bars[i].value / maxVal) * barAreaW : 0.0;

            // Bar fill
            double intensity = 0.5 + 0.5 * (bars[i].value / (maxVal > 0 ? maxVal : 1.0));
            PDFColor barColor = {
                cfg_.accent().r * intensity,
                cfg_.accent().g * intensity,
                cfg_.accent().b * intensity + (1.0 - intensity) * 0.4
            };
            s.setFillColor(barColor);
            s.rect(barAreaLeft, by, barW, barHeight, true, false);

            // Bar outline
            s.setStrokeColor(cfg_.accent());
            s.setLineWidth(0.3);
            s.rect(barAreaLeft, by, barW, barHeight, false, true);

            // Label (left of axis)
            s.setFillColor(Colors::DarkGray);
            s.text(left, by + 6, "F1", 8, bars[i].label);

            // Value (right of bar)
            std::string valStr = PDFUtil::formatNumber(bars[i].value, 2, cfg_.currencySymbol);
            s.setFillColor(Colors::Black);
            s.text(valueColX, by + 6, "F1", 8, valStr);
        }

        // Grid lines at 25%, 50%, 75%, 100%
        s.setStrokeColor(Colors::LightGray);
        s.setLineWidth(0.3);
        for (int pct : {25, 50, 75, 100}) {
            double gx = barAreaLeft + barAreaW * pct / 100.0;
            double gy = chartTop - static_cast<double>(bars.size()) * (barHeight + barGap) - 10;
            s.line(gx, chartTop, gx, gy);
            s.setFillColor(Colors::MidGray);
            s.text(gx - 8, gy - 12, "F1", 7, std::to_string(pct) + "%");
        }

        renderFooter(s, pageNum, totalPages, timestamp);
        if (cfg_.showPageBorder) renderPageBorder(s);
        renderWatermark(s);
        return s.str();
    }

private:
    const PDFConfig& cfg_;
    double contentArea_ = 0.0;

    // ── Shared Page Elements ──────────────────────────────────────────────────

    void renderFooter(PDFStream& s, int pageNum, int totalPages,
                      const std::string& timestamp) const
    {
        double y = cfg_.marginBottom - 24;
        // Separator line
        s.setStrokeColor(Colors::MidGray);
        s.setLineWidth(0.5);
        s.line(cfg_.marginLeft, y + 14,
               cfg_.pageWidth - cfg_.marginRight, y + 14);

        // Left: company + note
        s.setFillColor(Colors::DarkGray);
        s.text(cfg_.marginLeft, y, "F1", 8,
               cfg_.companyName + "  —  " + cfg_.footerNote);

        // Right: page number
        std::string pg = (totalPages > 0)
            ? ("Page " + std::to_string(pageNum) + " of " + std::to_string(totalPages))
            : ("Page " + std::to_string(pageNum));
        s.text(cfg_.pageWidth - cfg_.marginRight - 60, y, "F1", 8, pg);

        // Center: timestamp
        s.text(cfg_.pageWidth / 2.0 - 50, y, "F1", 7, timestamp);
    }

    void renderPageBorder(PDFStream& s) const {
        s.setStrokeColor(cfg_.accent());
        s.setLineWidth(1.5);
        s.rect(cfg_.marginLeft - 12,
               cfg_.marginBottom - 12,
               cfg_.pageWidth  - cfg_.marginLeft - cfg_.marginRight + 24,
               cfg_.pageHeight - cfg_.marginBottom - cfg_.marginTop  + 24,
               false, true);
        // Inner thin rule
        s.setLineWidth(0.3);
        s.setStrokeColor(Colors::MidGray);
        s.rect(cfg_.marginLeft - 8,
               cfg_.marginBottom - 8,
               cfg_.pageWidth  - cfg_.marginLeft - cfg_.marginRight + 16,
               cfg_.pageHeight - cfg_.marginBottom - cfg_.marginTop  + 16,
               false, true);
    }

    void renderWatermark(PDFStream& s) const {
        std::string wText;
        switch (cfg_.watermark) {
            case WatermarkStyle::Confidential: wText = "CONFIDENTIAL";  break;
            case WatermarkStyle::Draft:        wText = "DRAFT";         break;
            case WatermarkStyle::TopSecret:    wText = "TOP SECRET";    break;
            case WatermarkStyle::Copy:         wText = "COPY";          break;
            default: return;
        }
        // Light gray, large, rotated 45 degrees across center of page
        s.setFillColor(PDFColor{0.80, 0.80, 0.80});
        s.textRotated(cfg_.pageWidth / 2.0 - 110, cfg_.pageHeight / 2.0 - 40,
                      "F2", 54, wText, 45.0);
    }
};

// =============================================================================
//  SECTION 8: PDF Object Assembler
// =============================================================================

/// Assembles raw content streams into a valid PDF byte stream.
class PDFAssembler {
public:
    std::string build(const std::vector<std::string>& pageStreams,
                      const PDFConfig& cfg,
                      const std::tm& tm)
    {
        std::size_t numPages = pageStreams.size();

        // Object numbering:
        //   1     = Font Helvetica       (F1)
        //   2     = Font Helvetica-Bold  (F2)
        //   3     = Pages dictionary
        //   4..3+N = Page objects
        //   4+N..3+2N = Content streams
        //   4+2N  = Catalog
        //   5+2N  = Info

        const std::size_t fontF1     = 1;
        const std::size_t fontF2     = 2;
        const std::size_t pagesObj   = 3;
        const std::size_t firstPage  = 4;
        const std::size_t firstCont  = 4 + numPages;
        const std::size_t catalogObj = 4 + 2 * numPages;
        const std::size_t infoObj    = 5 + 2 * numPages;

        std::ostringstream pdf;
        pdf << "%PDF-1.4\n";

        std::vector<long> offsets;

        auto writeObj = [&](const std::string& body) {
            offsets.push_back(static_cast<long>(pdf.tellp()));
            pdf << body;
        };

        // Font F1: Helvetica
        {
            std::ostringstream o;
            o << fontF1 << " 0 obj\n"
              << "<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica "
              << "/Encoding /WinAnsiEncoding >>\nendobj\n";
            writeObj(o.str());
        }
        // Font F2: Helvetica-Bold
        {
            std::ostringstream o;
            o << fontF2 << " 0 obj\n"
              << "<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica-Bold "
              << "/Encoding /WinAnsiEncoding >>\nendobj\n";
            writeObj(o.str());
        }
        // Pages dict
        {
            std::ostringstream kids;
            kids << "[";
            for (std::size_t i = 0; i < numPages; ++i) {
                kids << (firstPage + i) << " 0 R";
                if (i + 1 < numPages) kids << " ";
            }
            kids << "]";
            std::ostringstream o;
            o << pagesObj << " 0 obj\n"
              << "<< /Type /Pages /Kids " << kids.str()
              << " /Count " << numPages << " >>\nendobj\n";
            writeObj(o.str());
        }
        // Page objects
        for (std::size_t i = 0; i < numPages; ++i) {
            std::ostringstream o;
            o << (firstPage + i) << " 0 obj\n"
              << "<< /Type /Page /Parent " << pagesObj << " 0 R "
              << "/MediaBox [0 0 "
              << std::fixed << std::setprecision(1)
              << cfg.pageWidth << " " << cfg.pageHeight << "] "
              << "/Resources << /Font << "
              << "/F1 " << fontF1 << " 0 R "
              << "/F2 " << fontF2 << " 0 R "
              << ">> >> "
              << "/Contents " << (firstCont + i) << " 0 R "
              << ">>\nendobj\n";
            writeObj(o.str());
        }
        // Content streams
        for (std::size_t i = 0; i < numPages; ++i) {
            const std::string& stream = pageStreams[i];
            std::ostringstream o;
            o << (firstCont + i) << " 0 obj\n"
              << "<< /Length " << stream.size() << " >>\n"
              << "stream\n"
              << stream
              << "endstream\nendobj\n";
            writeObj(o.str());
        }
        // Catalog
        {
            std::ostringstream o;
            o << catalogObj << " 0 obj\n"
              << "<< /Type /Catalog /Pages " << pagesObj << " 0 R >>\nendobj\n";
            writeObj(o.str());
        }
        // Info
        {
            std::ostringstream o;
            o << infoObj << " 0 obj\n"
              << "<< /Title (" << PDFUtil::escapePDF(cfg.reportTitle + " - " + cfg.companyName) << ") "
              << "/Author (" << PDFUtil::escapePDF(cfg.authorName) << ") "
              << "/Subject (" << PDFUtil::escapePDF(cfg.reportSubtitle) << ") "
              << "/Producer (Custom C++ PDF Generator) "
              << "/CreationDate (" << PDFUtil::formatPDFDate(tm) << ") "
              << ">>\nendobj\n";
            writeObj(o.str());
        }

        // Cross-reference table
        long xrefPos = static_cast<long>(pdf.tellp());
        std::size_t totalObj = infoObj;
        pdf << "xref\n0 " << (totalObj + 1) << "\n";
        pdf << "0000000000 65535 f \n";   // free entry, exactly 20 bytes with \n
        for (long off : offsets) {
            pdf << std::setw(10) << std::setfill('0') << off << " 00000 n \n";
        }
        pdf << "trailer\n"
            << "<< /Size " << (totalObj + 1)
            << " /Root " << catalogObj << " 0 R"
            << " /Info " << infoObj    << " 0 R >>\n";
        pdf << "startxref\n" << xrefPos << "\n%%EOF\n";

        return pdf.str();
    }
};

// =============================================================================
//  SECTION 9: Main PDF Generation Entry Point
// =============================================================================

/// Generate a multi-section PDF report and write it to `filename`.
/// Returns true on success.
bool generatePDFReport(const std::string& filename,
                       const PDFConfig& cfg,
                       std::vector<ReportSection> sections)
{
    if (sections.empty()) {
        std::cerr << "[PDF] No sections provided." << std::endl;
        return false;
    }

    // Capture current time
    std::time_t now = std::time(nullptr);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &now);
#else
    localtime_r(&now, &tm);
#endif
    std::string timestamp = PDFUtil::formatTimestamp(tm);

    // Build date range string
    std::string dateRange;
    if (!cfg.fromDate.empty() && !cfg.toDate.empty())
        dateRange = "Date Range: " + cfg.fromDate + " to " + cfg.toDate;
    else if (!cfg.fromDate.empty())
        dateRange = "From: " + cfg.fromDate;
    else if (!cfg.toDate.empty())
        dateRange = "To: " + cfg.toDate;
    else
        dateRange = "Date Range: Not specified";

    const double contentW = cfg.pageWidth - cfg.marginLeft - cfg.marginRight;

    // Pre-compute layouts and stats for every section
    struct SectionMeta {
        ColumnLayout layout;
        std::vector<ColumnStats> stats;
        std::size_t dataRows = 0;
    };
    std::vector<SectionMeta> metas(sections.size());
    for (std::size_t s = 0; s < sections.size(); ++s) {
        auto& m = metas[s];
        m.layout.compute(sections[s].rows, contentW, cfg.maxColumns);
        m.stats = computeStats(sections[s].rows, m.layout.isNumeric, m.layout.numCols);
        m.dataRows = sections[s].rows.size() > 1 ? sections[s].rows.size() - 1 : 0;
    }

    // ── Compute pagination ──────────────────────────────────────────────────
    // Each section can span multiple table pages, plus an optional chart page.
    const double topFirst = cfg.pageHeight - cfg.marginTop - 60;  // after section title banner
    const double topOther = cfg.pageHeight - cfg.marginTop;
    const double bottomY  = cfg.marginBottom + 40;                 // leave room for footer

    auto rowsPerPage = [&](double top) -> std::size_t {
        // Reserve 4 rows at bottom for stats block (if shown)
        double statsReserve = cfg.showSummaryStats ? cfg.rowHeight * 3 : 0.0;
        int fit = static_cast<int>((top - bottomY - statsReserve) / cfg.rowHeight) - 1;
        return static_cast<std::size_t>(std::max(fit, 1));
    };

    struct SectionPagePlan {
        std::size_t firstGlobalPage = 0;  // 1-based global page index
        std::vector<std::pair<std::size_t, std::size_t>> dataChunks; // (startRow, count)
        bool hasChartPage = false;
    };
    std::vector<SectionPagePlan> plans(sections.size());

    int fixedPages = 0;
    if (cfg.showCoverPage) ++fixedPages;
    if (cfg.showTOC)       ++fixedPages;

    // First pass: assign page ranges
    std::vector<std::string> pageStreams;
    pageStreams.reserve(fixedPages + sections.size() * 4);

    // Placeholder for cover and TOC (filled later when total page count is known)
    if (cfg.showCoverPage) pageStreams.push_back("");
    if (cfg.showTOC)       pageStreams.push_back("");

    std::vector<int> sectionFirstPage(sections.size(), 0);

    PageRenderer renderer(cfg);

    for (std::size_t si = 0; si < sections.size(); ++si) {
        auto& plan = plans[si];
        plan.firstGlobalPage = pageStreams.size() + 1; // 1-based
        sectionFirstPage[si] = static_cast<int>(plan.firstGlobalPage);

        std::size_t totalData = metas[si].dataRows;
        std::size_t startRow  = 1; // data starts at row index 1
        bool firstPage = true;

        while (startRow <= totalData) {
            double pageTop = firstPage ? topFirst : topOther;
            std::size_t count = std::min(rowsPerPage(pageTop), totalData - (startRow - 1));
            if (count == 0) break;
            plan.dataChunks.push_back({startRow, count});
            startRow += count;
            firstPage = false;
        }
        if (plan.dataChunks.empty()) {
            // Empty section: add one blank content page
            plan.dataChunks.push_back({1, 0});
        }

        // Add placeholder streams (filled in the second pass below)
        for (std::size_t c = 0; c < plan.dataChunks.size(); ++c)
            pageStreams.push_back("");

        // Chart page
        plan.hasChartPage = cfg.showBarChart && sections[si].chartColumnIndex >= 0
                            && !sections[si].rows.empty() && sections[si].rows.size() > 1;
        if (plan.hasChartPage) pageStreams.push_back("");
    }

    int totalPages = static_cast<int>(pageStreams.size());

    // ── Second pass: render each page stream ────────────────────────────────
    // Cover page
    if (cfg.showCoverPage) {
        pageStreams[0] = renderer.renderCoverPage(timestamp, dateRange, sections);
    }
    // TOC page
    if (cfg.showTOC) {
        int tocIdx = cfg.showCoverPage ? 1 : 0;
        pageStreams[static_cast<std::size_t>(tocIdx)] =
            renderer.renderTOCPage(sections, sectionFirstPage, timestamp);
    }

    // Section table & chart pages
    for (std::size_t si = 0; si < sections.size(); ++si) {
        auto& plan  = plans[si];
        auto& meta  = metas[si];
        auto& sec   = sections[si];
        std::size_t numChunks = plan.dataChunks.size();
        std::size_t baseIdx   = plan.firstGlobalPage - 1;

        for (std::size_t c = 0; c < numChunks; ++c) {
            auto [startRow, count] = plan.dataChunks[c];
            bool isFirst = (c == 0);
            bool isLast  = (c == numChunks - 1);
            int pageNum  = static_cast<int>(baseIdx + c + 1);

            pageStreams[baseIdx + c] = renderer.renderTablePage(
                sec, meta.layout, startRow, count,
                isFirst, meta.stats, isLast,
                pageNum, totalPages, timestamp);
        }

        if (plan.hasChartPage) {
            std::size_t chartIdx = baseIdx + numChunks;
            int chartPageNum = static_cast<int>(chartIdx + 1);
            pageStreams[chartIdx] = renderer.renderBarChartPage(
                sec,
                static_cast<std::size_t>(sec.chartColumnIndex),
                meta.layout,
                chartPageNum, totalPages, timestamp);
        }
    }

    // ── Assemble and write PDF ───────────────────────────────────────────────
    PDFAssembler assembler;
    std::string pdfData = assembler.build(pageStreams, cfg, tm);

    std::ofstream out(filename, std::ios::binary);
    if (!out) {
        std::cerr << "[PDF] Cannot open output file: " << filename << std::endl;
        return false;
    }
    out.write(pdfData.data(), static_cast<std::streamsize>(pdfData.size()));
    if (!out.good()) {
        std::cerr << "[PDF] Write error for: " << filename << std::endl;
        return false;
    }
    std::cout << "[PDF] Written " << pdfData.size() << " bytes -> " << filename
              << "  (" << totalPages << " pages, "
              << sections.size() << " section(s))" << std::endl;
    return true;
}

// =============================================================================
//  SECTION 10: Convenience Wrapper — single-table shorthand
// =============================================================================

/// Drop-in replacement for the original makepdffile(), now bug-fixed.
/// Creates a single-section PDF with all advanced features enabled.
void makepdffile(const std::string& filename,
                 const std::string& reportType,
                 const std::string& fromDate,
                 const std::string& toDate,
                 const std::vector<std::vector<std::string>>& tableData)
{
    PDFConfig cfg;
    cfg.reportTitle   = "Project Report";
    cfg.reportSubtitle = reportType.empty() ? "General Report" : PDFUtil::capitalize(PDFUtil::toLower(reportType));
    cfg.fromDate      = fromDate;
    cfg.toDate        = toDate;

    // BUG FIX: original called tolower(std::string) which does not compile.
    // Fixed by using PDFUtil::toLower() which uses std::transform correctly.
    std::string typeLower = PDFUtil::toLower(reportType);
    if      (typeLower == "sales")    cfg.theme = ReportTheme::Sales;
    else if (typeLower == "finance")  cfg.theme = ReportTheme::Finance;
    else if (typeLower == "hr")       cfg.theme = ReportTheme::HR;
    else if (typeLower == "tech"
          || typeLower == "technical") cfg.theme = ReportTheme::Technical;
    else if (typeLower == "executive") cfg.theme = ReportTheme::Executive;

    ReportSection section;
    section.title            = cfg.reportSubtitle;
    section.rows             = tableData;
    section.chartColumnIndex = -1; // no chart by default in compat mode
    section.showStats        = true;

    // Try to find a numeric column for chart
    if (tableData.size() > 1) {
        for (std::size_t c = 1; c < tableData[0].size(); ++c) {
            if (!tableData[1].empty() && c < tableData[1].size()
                && PDFUtil::isNumericStr(tableData[1][c])) {
                section.chartColumnIndex = static_cast<int>(c);
                break;
            }
        }
    }

    generatePDFReport(filename, cfg, {section});
}

// =============================================================================
//  SECTION 11: Demo / main()
// =============================================================================

int main() {
    std::cout << "=== PDF Report Generator Demo ===" << std::endl;

    // ── Demo 1: Basic single-table (compatibility shim) ─────────────────────
    {
        std::vector<std::vector<std::string>> table = {
            {"ID", "Project Name", "Budget", "Spent", "Status"},
            {"1",  "Alpha Platform",    "50,000",  "32,150",  "Active"},
            {"2",  "Beta Mobile App",   "25,000",  "25,000",  "Complete"},
            {"3",  "Gamma Dashboard",   "15,000",  "8,400",   "In Progress"},
            {"4",  "Delta API",         "10,000",  "1,200",   "Pending"},
            {"5",  "Epsilon Analytics", "80,000",  "80,000",  "Complete"},
            {"6",  "Zeta Infra",        "35,000",  "35,000",  "Failed"},
            {"7",  "Eta Security Audit","12,000",  "3,600",   "Active"},
            {"8",  "Theta ML Pipeline", "60,000",  "22,000",  "In Progress"},
        };
        makepdffile("/home/claude/demo1_basic.pdf", "technical", "2025-01-01", "2025-06-30", table);
    }

    // ── Demo 2: Sales Report with watermark ─────────────────────────────────
    {
        PDFConfig cfg;
        cfg.companyName     = "Shree Krishna Estate";
        cfg.reportTitle     = "Quarterly Sales Report";
        cfg.reportSubtitle  = "Q2 FY 2025-26";
        cfg.authorName      = "Sales Team";
        cfg.footerNote      = "Confidential — Sales Department Use Only";
        cfg.fromDate        = "2025-04-01";
        cfg.toDate          = "2025-06-30";
        cfg.theme           = ReportTheme::Sales;
        cfg.watermark       = WatermarkStyle::Confidential;
        cfg.currencySymbol  = "$";
        cfg.showBarChart    = true;

        ReportSection salesSection;
        salesSection.title       = "Property Sales Summary";
        salesSection.description = "All residential and commercial transactions for Q2 FY2025-26.";
        salesSection.chartColumnIndex = 3; // Sale Price column
        salesSection.rows = PDFUtil::parseCSV(
            "Sr,Property,Type,Sale Price,Commission,Closed Date,Agent\n"
            "1,Sector 12 Plot,Residential,4200000,126000,2025-04-05,Rajesh K.\n"
            "2,MG Road Shop,Commercial,8500000,255000,2025-04-18,Priya S.\n"
            "3,Green Valley Villa,Residential,12000000,360000,2025-05-02,Amit T.\n"
            "4,Industrial Plot B7,Commercial,6800000,204000,2025-05-15,Rajesh K.\n"
            "5,Sector 45 Flat 3B,Residential,3500000,105000,2025-05-28,Neha M.\n"
            "6,Tech Park Office,Commercial,22000000,660000,2025-06-10,Priya S.\n"
            "7,Hill View Bungalow,Residential,9500000,285000,2025-06-22,Amit T.\n"
            "8,Warehouse Unit 5,Commercial,5200000,156000,2025-06-30,Neha M.\n"
        );

        ReportSection agentSection;
        agentSection.title       = "Agent Performance";
        agentSection.description = "Commission earned per agent this quarter.";
        agentSection.chartColumnIndex = 2;
        agentSection.rows = PDFUtil::parseCSV(
            "Agent,Transactions,Total Commission,Avg Deal Size\n"
            "Rajesh K.,2,330000,5500000\n"
            "Priya S.,2,915000,15250000\n"
            "Amit T.,2,645000,10750000\n"
            "Neha M.,2,261000,4350000\n"
        );

        generatePDFReport("/home/claude/demo2_sales.pdf", cfg,
                          {salesSection, agentSection});
    }

    // ── Demo 3: Finance Report with Draft watermark ──────────────────────────
    {
        PDFConfig cfg;
        cfg.companyName    = "DevPrompt Technologies";
        cfg.reportTitle    = "Annual Expense Report";
        cfg.reportSubtitle = "FY 2025 — Final Review";
        cfg.authorName     = "Finance Controller";
        cfg.fromDate       = "2025-01-01";
        cfg.toDate         = "2025-12-31";
        cfg.theme          = ReportTheme::Finance;
        cfg.watermark      = WatermarkStyle::Draft;
        cfg.currencySymbol = "$";

        ReportSection expenseSection;
        expenseSection.title = "Departmental Expenses";
        expenseSection.description = "Breakdown of operational expenses by department.";
        expenseSection.chartColumnIndex = 2;
        expenseSection.rows = PDFUtil::parseCSV(
            "Department,Head Count,Q1 Spend,Q2 Spend,Q3 Spend,Q4 Spend\n"
            "Engineering,24,580000,612000,598000,650000\n"
            "Marketing,8,210000,280000,195000,310000\n"
            "HR & Admin,5,90000,92000,88000,95000\n"
            "Finance,4,70000,72000,69000,74000\n"
            "Operations,12,320000,340000,315000,360000\n"
            "Legal,3,45000,60000,42000,48000\n"
        );

        ReportSection budgetSection;
        budgetSection.title = "Budget vs Actual";
        budgetSection.description = "Year-to-date budget adherence summary.";
        budgetSection.chartColumnIndex = 3;
        budgetSection.rows = PDFUtil::parseCSV(
            "Category,Budgeted,Actual,Variance,Status\n"
            "Salaries,8500000,8320000,180000,On Hold\n"
            "Infrastructure,1200000,1380000,-180000,Active\n"
            "Software Licenses,450000,430000,20000,Complete\n"
            "Travel,200000,145000,55000,Active\n"
            "Marketing Campaigns,600000,680000,-80000,Pending\n"
            "R&D,900000,870000,30000,Active\n"
            "Training,150000,120000,30000,Complete\n"
        );

        generatePDFReport("/home/claude/demo3_finance.pdf", cfg,
                          {expenseSection, budgetSection});
    }

    // ── Demo 4: Large table (multi-page) with HR theme ───────────────────────
    {
        PDFConfig cfg;
        cfg.companyName    = "GigVibe Freelancing Platform";
        cfg.reportTitle    = "HR Employee Directory";
        cfg.reportSubtitle = "All Active Employees — June 2025";
        cfg.authorName     = "HR Department";
        cfg.fromDate       = "2025-06-01";
        cfg.toDate         = "2025-06-30";
        cfg.theme          = ReportTheme::HR;
        cfg.watermark      = WatermarkStyle::None;
        cfg.showBarChart   = false; // no chart for directory

        ReportSection emp;
        emp.title       = "Employee Directory";
        emp.description = "Paginated across multiple pages — 40+ employees.";
        emp.rows.push_back({"ID", "Name", "Department", "Role", "Salary", "Status"});

        // Generate 42 sample rows to test multi-page pagination
        std::vector<std::string> depts = {"Engineering","Marketing","HR","Finance","Operations","Legal"};
        std::vector<std::string> roles = {"Sr. Engineer","Manager","Analyst","Developer","Lead","Director"};
        std::vector<std::string> statuses = {"Active","On Hold","Active","Active","Active","Pending"};
        for (int i = 1; i <= 42; ++i) {
            std::string dept   = depts[(i - 1) % depts.size()];
            std::string role   = roles[(i - 1) % roles.size()];
            std::string status = statuses[(i - 1) % statuses.size()];
            int salary = 60000 + (i % 7) * 8000 + (i % 3) * 5000;
            emp.rows.push_back({
                std::to_string(1000 + i),
                "Employee " + std::to_string(i),
                dept, role,
                PDFUtil::formatNumber(salary, 0, "$"),
                status
            });
        }
        emp.chartColumnIndex = -1;

        generatePDFReport("/home/claude/demo4_hr_multipage.pdf", cfg, {emp});
    }

    // ── Demo 5: Empty table edge case ────────────────────────────────────────
    {
        makepdffile("/home/claude/demo5_empty.pdf", "", "", "", {});
    }

    // ── Demo 6: Special characters in PDF strings ────────────────────────────
    {
        PDFConfig cfg;
        cfg.companyName    = "Test (QA) Dept\\Division";
        cfg.reportTitle    = "Special (Chars) Test";
        cfg.reportSubtitle = "Verifying PDF escape: ( ) \\";
        cfg.theme          = ReportTheme::Executive;
        cfg.showBarChart   = false;

        ReportSection sec;
        sec.title = "Escape Test Table";
        sec.rows = {
            {"Field", "Value (Raw)", "Status"},
            {"Path",  "C:\\Users\\ketan\\docs", "Active"},
            {"Query", "SELECT (id, name)", "Complete"},
            {"Note",  "100% (done) & verified", "Active"},
        };
        generatePDFReport("/home/claude/demo6_special_chars.pdf", cfg, {sec});
    }

    std::cout << "\nAll demos complete." << std::endl;
    return 0;
}