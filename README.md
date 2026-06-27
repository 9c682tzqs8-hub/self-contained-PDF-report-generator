# self-contained-PDF-report-generator
A self-contained, dependency-free C++17 library for generating highly formatted, paginated PDF reports and charts from raw data.
# C++ PDF Report Generator

A lightweight, self-contained PDF generator written in plain C++17. This project allows you to create highly formatted, paginated PDF documents directly from raw data (like CSV strings or 2D vectors) without needing any heavy third-party dependencies like libharu, PDFium, or Cairo. 

It handles everything from math calculations for column widths to raw byte stream assembly and cross-reference (`xref`) table generation.

## ✨ Features
*   **Zero Dependencies:** Built entirely with standard C++17.
*   **Dynamic Layouts:** Auto-fits column widths based on content length.
*   **Data Visualization:** Automatically renders horizontal bar charts from numeric columns.
*   **Summary Statistics:** Calculates and appends total, average, min, and max rows.
*   **Smart Formatting:** Right-aligns numeric columns, adds alternating row shading, and color-codes status cells (e.g., Active, Pending, Complete).
*   **Professional Document Features:** Supports cover pages, automated Tables of Contents, custom headers/footers, watermarks (e.g., CONFIDENTIAL, DRAFT), and page borders.
*   **Theming:** Pre-built color themes for Sales, Finance, HR, Technical, and Executive reports.

## 🚀 Use Cases
This tool is perfect for backend systems that need to export raw data into clean business documents. Example use cases include:
*   Generating user invoices or financial quarterly reports.
*   Exporting HR directories or payroll summaries.
*   Building dynamic system metrics and platform usage reports (e.g., exporting freelancer activity logs for platforms like GigVibe).

## 🛠️ Getting Started

### Prerequisites
* A C++17 compatible compiler (GCC, Clang, or MSVC).

### Compilation
Since this is a self-contained `.cpp` file, you can compile it directly from your terminal:

```bash
g++ -std=c++17 -o pdf_gen pdf_generator.cpp
./pdf_gen
```
### Quick Usage Example :
```bash
#include "pdf_generator.cpp" // Or compile together if you split into .h/.cpp
int main() {
    std::vector<std::vector<std::string>> table = {
        {"ID", "Project Name", "Budget", "Status"},
        {"1",  "Alpha Platform", "50,000", "Active"},
        {"2",  "Beta App",       "25,000", "Complete"}
    };

    // Generates a Technical themed PDF from the table data
    makepdffile("output_report.pdf", "technical", "2025-01-01", "2025-06-30", table);
    
    return 0;
}
```
### 📂 Demos Included
*   The main() function in the source code includes several demonstrations:
*   Basic Table: A simple single-table compatibility mode.
*   Sales Report: Includes a watermark, multiple sections, and a commission chart.
*   Finance Report: Demonstrates budget vs. actuals with summary statistics.
*   HR Directory: Showcases pagination across multiple pages for large datasets.
### 👤 Author
```
   Ketan Yadav
```
### 📄 License
*   This project is open-source and available under the MIT License.
