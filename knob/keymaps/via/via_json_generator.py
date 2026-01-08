INPUT_PATH = 'via.template'
OUTPUT_PATH = 'via.json'

START_MARKER = 'START_COPY_SECTION'
END_MARKER = 'END_COPY_SECTION'
REPLACEMENT_KEY = '<LAYER>'
NUM_LAYERS = 8

with open(INPUT_PATH, 'r') as f:
    lines = f.readlines()
lines_before = []
lines_during = []
lines_after = []
current_state = 'before'
for line in lines:
    if START_MARKER in line:
        current_state = 'during'
    elif END_MARKER in line:
        current_state = 'after'
    elif current_state == 'before':
        lines_before.append(line)
    elif current_state == 'during':
        lines_during.append(line)
    elif current_state == 'after':
        lines_after.append(line)

lines_final = []
lines_final.extend(lines_before)
for layer_idx in range(NUM_LAYERS):
    for line in lines_during:
        lines_final.append(line.replace(REPLACEMENT_KEY, str(layer_idx)))
    if layer_idx != NUM_LAYERS - 1:
        lines_final.append(',\n')
lines_final.extend(lines_after)

with open(OUTPUT_PATH, 'w') as f:
    f.writelines(lines_final)


