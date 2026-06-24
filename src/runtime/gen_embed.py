#!/usr/bin/env python3
"""Generate clgui_embed.h from clgui.c + apple_model.h."""
import os

def escape_c_string(text):
    result = []
    for ch in text:
        if ch == '"':
            result.append('\\"')
        elif ch == '\\':
            result.append('\\\\')
        elif ch == '\n':
            result.append('\\n')
        else:
            result.append(ch)
    return ''.join(result)

def main():
    script_dir = os.path.dirname(os.path.abspath(__file__))
    src_path = os.path.join(script_dir, "clgui.c")
    model_path = os.path.join(script_dir, "apple_model.h")
    out_path = os.path.join(script_dir, "clgui_embed.h")

    with open(src_path, 'r') as f:
        clgui_content = f.read()

    with open(model_path, 'r') as f:
        model_content = f.read()

    # Replace #include "apple_model.h" with the actual content
    clgui_content = clgui_content.replace('#include "apple_model.h"', model_content)

    out_lines = []
    out_lines.append('#ifndef CLGUI_EMBED_H')
    out_lines.append('#define CLGUI_EMBED_H')
    out_lines.append('static const char *clgui_embedded =')

    text_lines = clgui_content.split('\n')
    for i, line in enumerate(text_lines):
        escaped = escape_c_string(line)
        if i < len(text_lines) - 1:
            out_lines.append(f'  "{escaped}\\n"')
        else:
            out_lines.append(f'  "{escaped}\\n"')

    out_lines.append(';')
    out_lines.append('#endif')

    with open(out_path, 'w') as f:
        f.write('\n'.join(out_lines) + '\n')

    print(f"Generated {out_path}")
    print(f"  Size: {os.path.getsize(out_path)} bytes")

if __name__ == "__main__":
    main()
