from PIL import Image

img_curr = Image.open('scratch/current_file_render.png')
img_new = Image.open('scratch/rudo_idle_f1_perfect.png')

w, h = img_curr.size
comp = Image.new('RGB', (w * 2 + 20, h), (200, 200, 200))
comp.paste(img_curr, (0, 0))
comp.paste(img_new, (w + 20, 0))
comp.save('scratch/side_by_side_comparison.png')
print('Comparison saved!')
