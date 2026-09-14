import markdown
from xhtml2pdf import pisa
import sys
import argparse
import urllib.parse
import re

def preprocess_markdown(text):
    lines = text.split('\n')
    processed_lines = []
    
    list_pattern = re.compile(r'^\s*([-*]|\d+\.)\s')
    
    for i, line in enumerate(lines):
        # If this line is a list item
        if list_pattern.match(line):
            # If the previous line has text and is NOT a list item, insert a blank line
            if i > 0 and lines[i-1].strip() != '' and not list_pattern.match(lines[i-1]) and not lines[i-1].startswith('#'):
                processed_lines.append('')
        processed_lines.append(line)
        
    return '\n'.join(processed_lines)

def convert_md_to_pdf(md_file, pdf_file, paper_size="letter"):
    with open(md_file, 'r', encoding='utf-8') as f:
        text = f.read()
        
    # Fix lists that don't have blank lines before them
    text = preprocess_markdown(text)
    
    html_content = markdown.markdown(text, extensions=['tables', 'fenced_code', 'sane_lists'])
    
    # [PATCH] Fix xhtml2pdf image rendering bugs
    # 1. xhtml2pdf does not support URL-encoded local paths (like %20 for spaces)
    def decode_src(match):
        full_tag = match.group(0)
        src_url = match.group(1)
        decoded_url = urllib.parse.unquote(src_url)
        return full_tag.replace(src_url, decoded_url)
    html_content = re.sub(r'src="([^"]+)"', decode_src, html_content)
    
    # 2. xhtml2pdf does not support percentage widths (e.g. width="32%")
    # We replace them with a fixed pixel width, scaling based on the percentage
    def scale_width(match):
        percent = int(match.group(1))
        # A4/Letter page width is ~500px usable. 30% -> 150px, 48% -> 240px
        pixel_width = int((percent / 100.0) * 500)
        return f'width="{pixel_width}"'
    html_content = re.sub(r'width="(\d+)%"', scale_width, html_content)
    
    # 3. Fix relative links to point to the GitHub repository so they actually work in the PDF
    repo_url = "https://github.com/rguilmet/magnetic-field-scanner/blob/main/"
    def fix_relative_links(match):
        full_tag = match.group(0)
        href = match.group(1)
        if href.startswith("http") or href.startswith("#") or href.startswith("mailto:"):
            return full_tag
        return full_tag.replace(href, repo_url + href)
        
    html_content = re.sub(r'href="([^"]+)"', fix_relative_links, html_content)
    
    html_template = f"""
    <html>
    <head>
    <style>
        @page {{ size: {paper_size} portrait; margin: 2cm; }}
        body {{ font-family: Helvetica, Arial, sans-serif; font-size: 11pt; line-height: 1.5; color: #333; }}
        h1, h2, h3 {{ color: #2c3e50; }}
        h1 {{ border-bottom: 2px solid #ecf0f1; padding-bottom: 5px; }}
        h2 {{ border-bottom: 1px solid #ecf0f1; padding-bottom: 3px; margin-top: 20px; }}
        a {{ color: #2980b9; text-decoration: none; border-bottom: 1px solid #2980b9; }}
        table {{ width: 100%; border-collapse: collapse; margin: 15px 0; }}
        th, td {{ border: 1px solid #bdc3c7; padding: 8px; text-align: left; }}
        th {{ background-color: #ecf0f1; }}
        code {{ background-color: #f8f9fa; padding: 2px 4px; border-radius: 4px; font-family: monospace; font-size: 9pt; }}
        pre {{ background-color: #f8f9fa; padding: 10px; border-radius: 4px; font-family: monospace; font-size: 9pt; border: 1px solid #e9ecef; white-space: pre-wrap; }}
        blockquote {{ border-left: 4px solid #3498db; margin: 0; padding-left: 15px; color: #555; background-color: #f4f6f7; padding: 10px; }}
        li {{ margin-bottom: 5px; }}
        ul, ol {{ margin-top: 5px; margin-bottom: 5px; }}
    </style>
    </head>
    <body>
    {html_content}
    </body>
    </html>
    """
    
    with open(pdf_file, "w+b") as result_file:
        pisa_status = pisa.CreatePDF(html_template, dest=result_file)
    
    if pisa_status.err:
        print(f"Error converting to PDF: {pisa_status.err}")
    else:
        print(f"Successfully created {pdf_file}")

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Convert Markdown to PDF using xhtml2pdf.")
    parser.add_argument("input", help="Input markdown file")
    parser.add_argument("output", help="Output PDF file")
    parser.add_argument("--size", default="letter", help="Target paper size (e.g., 'letter', 'a4', 'legal'). Default is 'letter'.")
    
    args = parser.parse_args()
    
    convert_md_to_pdf(args.input, args.output, args.size)
