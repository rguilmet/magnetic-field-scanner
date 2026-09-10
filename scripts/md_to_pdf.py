import markdown
from xhtml2pdf import pisa
import sys

def convert_md_to_pdf(md_file, pdf_file):
    with open(md_file, 'r', encoding='utf-8') as f:
        text = f.read()
    
    html_content = markdown.markdown(text, extensions=['tables', 'fenced_code', 'sane_lists'])
    
    html_template = f"""
    <html>
    <head>
    <style>
        @page {{ size: a4 portrait; margin: 2cm; }}
        body {{ font-family: Helvetica, Arial, sans-serif; font-size: 11pt; line-height: 1.5; color: #333; }}
        h1, h2, h3 {{ color: #2c3e50; }}
        h1 {{ border-bottom: 2px solid #ecf0f1; padding-bottom: 5px; }}
        h2 {{ border-bottom: 1px solid #ecf0f1; padding-bottom: 3px; margin-top: 20px; }}
        table {{ width: 100%; border-collapse: collapse; margin: 15px 0; }}
        th, td {{ border: 1px solid #bdc3c7; padding: 8px; text-align: left; }}
        th {{ background-color: #ecf0f1; }}
        code {{ background-color: #f8f9fa; padding: 2px 4px; border-radius: 4px; font-family: monospace; font-size: 9pt; }}
        pre {{ background-color: #f8f9fa; padding: 10px; border-radius: 4px; font-family: monospace; font-size: 9pt; border: 1px solid #e9ecef; white-space: pre-wrap; }}
        blockquote {{ border-left: 4px solid #3498db; margin: 0; padding-left: 15px; color: #555; background-color: #f4f6f7; padding: 10px; }}
        li {{ margin-bottom: 5px; }}
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
    if len(sys.argv) < 3:
        print("Usage: python md_to_pdf.py <input.md> <output.pdf>")
    else:
        convert_md_to_pdf(sys.argv[1], sys.argv[2])
