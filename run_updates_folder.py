import re

with open("docs/folder_layout.txt", "r", encoding="utf-8") as f:
    doc = f.read()

doc = doc.replace("Board_Holder_REF&MID", "Board_Holder_REF&NEAR")

with open("docs/folder_layout.txt", "w", encoding="utf-8") as f:
    f.write(doc)
