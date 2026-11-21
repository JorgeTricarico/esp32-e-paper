import json

path = r'C:\Users\admin\.gemini\antigravity-cli\brain\ebfe51a8-9d8a-47de-8a10-70ef7d83a23d\.system_generated\logs\transcript.jsonl'
with open(path, 'r', encoding='utf-8') as f:
    for line in f:
        try:
            obj = json.loads(line)
            content = obj.get('content', '')
            if 'industrial' in content.lower() and obj.get('step_index', 0) < 950:
                print(f"Step {obj.get('step_index')}: {content[:300]}")
        except:
            pass
