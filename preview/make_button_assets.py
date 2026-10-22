"""Build original PS4 control PNGs from code; Pillow is development-only."""
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont, ImageFilter

OUT = Path(__file__).resolve().parents[1] / "pkg/assets/ui/buttons"
OUT.mkdir(parents=True, exist_ok=True)
SCALE = 4
SIZE = 128 * SCALE
INK = "#f0f5ff"
FONT = Path(__file__).parent / "assets/DejaVuSans.ttf"


def mask(kind, box, radius=16):
    result = Image.new("L", (SIZE, SIZE))
    painter = ImageDraw.Draw(result)
    if kind == "circle":
        painter.ellipse(tuple(round(v*SCALE) for v in box), fill=255)
    elif kind == "pad":
        painter.polygon([(round(x*SCALE), round(y*SCALE)) for x,y in box], fill=255)
    else:
        painter.rounded_rectangle(tuple(round(v*SCALE) for v in box), radius=radius*SCALE, fill=255)
    return result


def coat(image, shape, top, bottom):
    ramp = Image.linear_gradient("L").resize((SIZE, SIZE))
    finish = Image.composite(Image.new("RGBA", image.size, bottom), Image.new("RGBA", image.size, top), ramp)
    image.paste(finish, (0,0), shape)


def line(painter, points, color, width=6):
    painter.line([(round(x*SCALE),round(y*SCALE)) for x,y in points], fill=color,
                 width=round(width*SCALE), joint="curve")
    radius=width*SCALE/2
    for x,y in points:
        painter.ellipse((round(x*SCALE-radius),round(y*SCALE-radius),round(x*SCALE+radius),round(y*SCALE+radius)), fill=color)


def keycap(name):
    image = Image.new("RGBA", (SIZE,SIZE))
    if name in ("cross","circle","square","triangle"):
        shape=mask("circle",(8,10,120,122))
        shadow=Image.new("RGBA",image.size,(0,0,0,0))
        shadow.putalpha(shape.filter(ImageFilter.GaussianBlur(2*SCALE)).point(lambda value:value*3//5))
        image.alpha_composite(shadow)
        coat(image,shape,"#283343","#03070d")
        coat(image,mask("circle",(8,4,120,116)),"#a3b3ca","#243042")
        coat(image,mask("circle",(11,7,117,113)),"#48576d","#101722")
        face=mask("circle",(16,12,112,108))
        coat(image,face,"#334054","#0b111c")
        # A soft upper reflection leaves the symbol's centre high-contrast at TV sizes.
        shine=Image.new("RGBA",image.size,(255,255,255,0))
        shine.putalpha(Image.linear_gradient("L").resize(image.size).point(lambda value:max(0,25-value//3)))
        shine.putalpha(Image.composite(shine.getchannel("A"),Image.new("L",image.size),face))
        image.alpha_composite(shine)
    elif name == "dpad":
        outline=[(47,9),(81,9),(81,45),(117,45),(117,79),(81,79),(81,115),(47,115),(47,79),(11,79),(11,45),(47,45)]
        base=[(x,y+7) for x,y in outline]
        coat(image,mask("pad",base),"#182231","#04080f")
        coat(image,mask("pad",outline),"#9baac0","#29374b")
        inset=[(64+(x-64)*0.88,62+(y-62)*0.88) for x,y in outline]
        coat(image,mask("pad",inset),"#3c4c63","#101925")
    else:
        box=(7,30,121,103)
        coat(image,mask("round",(7,37,121,111),18),"#1e2b3d","#050a12")
        coat(image,mask("round",box,18),"#a2b2c8","#2a394e")
        coat(image,mask("round",(11,34,117,99),15),"#4a5b74","#101a28")
        coat(image,mask("round",(16,39,112,94),11),"#2c3b51","#101925")

    symbol=Image.new("RGBA",image.size)
    painter=ImageDraw.Draw(symbol)
    if name == "cross":
        for points in ([(42,38),(86,82)],[(86,38),(42,82)]):line(painter,points,INK,7)
    elif name == "circle":
        painter.ellipse(tuple(v*SCALE for v in (37,33,91,87)),outline=INK,width=7*SCALE)
    elif name == "square":
        line(painter,[(40,36),(88,36),(88,84),(40,84),(40,36)],INK,6)
    elif name == "triangle":
        line(painter,[(64,31),(94,84),(34,84),(64,31)],INK,6)
    elif name == "dpad":
        for points in ([(57,29),(64,22),(71,29)],[(57,95),(64,102),(71,95)],[(31,55),(24,62),(31,69)],[(97,55),(104,62),(97,69)]):
            line(painter,points,INK,4)
        painter.ellipse(tuple(v*SCALE for v in (59,57,69,67)),fill="#8094af")
    else:
        font=ImageFont.truetype(str(FONT),(19 if name=="options" else 42)*SCALE)
        painter.text((64*SCALE,65*SCALE),name.upper(),font=font,anchor="mm",fill=INK,stroke_width=SCALE//2)
    # Embossed symbols cast a small recessed shadow beneath the bright face.
    dark=Image.new("RGBA",image.size,(0,0,0,0))
    dark.putalpha(symbol.getchannel("A").filter(ImageFilter.GaussianBlur(SCALE//2)))
    image.alpha_composite(dark,(0,2*SCALE))
    image.alpha_composite(symbol)
    return image.resize((128,128),Image.Resampling.LANCZOS)


if __name__ == "__main__":
    for button in ("cross","circle","square","triangle","options","dpad","l1","r1"):
        keycap(button).save(OUT/f"{button}.png",optimize=True)
