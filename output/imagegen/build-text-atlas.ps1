param([switch]$Variation, [switch]$RepeatText, [switch]$MixedText)
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing
Add-Type -ReferencedAssemblies System.Drawing,System.Core -TypeDefinition @'
using System;
using System.IO;
using System.Text;
using System.Linq;
using System.Collections.Generic;
using System.Drawing;
using System.Drawing.Drawing2D;
using System.Drawing.Imaging;
public static class WarningAtlas {
 static void Pattern(Graphics g, float left, float right, float y, int kind, bool reverse) {
  // Whole symbols only; no clipped symbols at either end of a run.
  int pitch=kind==1 ? 44 : 48;
  int width=kind==1 ? 40 : 40;
  int count=(int)Math.Floor((right-left+pitch-width)/pitch);
  if(count<1) return;
  float start=left+(right-left-((count-1)*pitch+width))/2;
  for(int j=0;j<count;j++) {
   float x=start+j*pitch;
   PointF[] points;
   if(kind==1) points=new PointF[]{new PointF(x+18,y),new PointF(x+40,y),new PointF(x+22,y+48),new PointF(x,y+48)};
   else points=new PointF[]{new PointF(x,y),new PointF(x+18,y),new PointF(x+40,y+24),new PointF(x+18,y+48),new PointF(x,y+48),new PointF(x+22,y+24)};
   if(reverse) for(int k=0;k<points.Length;k++) points[k].X=x+40-(points[k].X-x);
   g.FillPolygon(Brushes.White,points);
  }
 }
 public static void Build(string input, string output, bool variation, bool repeatText, bool mixedText) {
  var rows = File.ReadAllLines(input, Encoding.UTF8).Where(s => s.Length > 3 && Char.IsDigit(s[0]) && Char.IsDigit(s[1])).Select(s => s.Substring(3).Replace(" >>>>", "")).ToArray();
  if(mixedText) {
   var phrases=File.ReadAllLines(Path.Combine(Path.GetDirectoryName(input),"text-atlas-five-phrases.txt"),Encoding.UTF8).Where(s=>!String.IsNullOrWhiteSpace(s)).ToArray();
   if(phrases.Length!=5) throw new Exception("Expected exactly five phrases");
   rows=Enumerable.Range(0,32).Select(i=>phrases[i%5]).ToArray();
  }
  if(rows.Length != 32) throw new Exception("Expected 32 rows");
  var paths = new List<GraphicsPath>();
  var family = new FontFamily("Malgun Gothic");
  var format = (StringFormat)StringFormat.GenericTypographic.Clone();
  float minY = float.MaxValue, maxY = float.MinValue;
  foreach(var row in rows) {
   var path = new GraphicsPath();
   float x=0;
   foreach(var phrase in row.Split(new string[]{"    "}, StringSplitOptions.None)) {
    var part = new GraphicsPath();
    part.AddString(phrase, family, (int)FontStyle.Bold, 64, new PointF(0,0), format);
    var b=part.GetBounds();
    using(var m=new Matrix()) { m.Translate(x-b.Left,0); part.Transform(m); }
    path.AddPath(part,false); x+=b.Width+64;
    part.Dispose();
   }
   var bounds=path.GetBounds(); minY=Math.Min(minY,bounds.Top); maxY=Math.Max(maxY,bounds.Bottom);
   paths.Add(path);
  }
  float scale=48f/(maxY-minY);
  using(var bitmap=new Bitmap(2048,2048,PixelFormat.Format24bppRgb)) {
   using(var g=Graphics.FromImage(bitmap)) {
    g.Clear(Color.Black); g.SmoothingMode=SmoothingMode.AntiAlias;
    for(int row=0;row<32;row++) {
     var path=paths[row]; var b=path.GetBounds();
     float textWidth=b.Width*scale;
     if(mixedText) {
      int layout=row%3;
      int copies=layout==2 ? 2 : 1;
      float cursor=layout==0 ? (2048-textWidth)/2 : 48;
      g.SetClip(new Rectangle(0,row*64+8,2048,48));
      for(int copy=0;copy<copies;copy++) {
       using(var text=(GraphicsPath)path.Clone()) {
        using(var m=new Matrix(scale,0,0,scale,cursor-b.Left*scale,row*64+8-minY*scale)) text.Transform(m);
        g.FillPath(Brushes.White,text);
       }
       cursor+=textWidth+64;
      }
      if(layout!=0) {
       if(cursor+136>2000) throw new Exception("Insufficient room for repeated symbols");
       Pattern(g,cursor,2000,row*64+8,0,false);
      }
      g.ResetClip();
      continue;
     }
     if(repeatText) {
      int kind=row%6==2 ? 1 : 0;
      bool reverse=row%6==4;
      float symbolWidth=kind==1 ? 40 : 88;
      float gap=symbolWidth+64;
      int repeats=Math.Min(4,(int)Math.Floor((1984+gap)/(textWidth+gap)));
      if(repeats<2) throw new Exception("Repeated text does not fit: "+row);
      float groupWidth=repeats*textWidth+(repeats-1)*gap;
      float cursor=(2048-groupWidth)/2;
      g.SetClip(new Rectangle(0,row*64+8,2048,48));
      for(int copy=0;copy<repeats;copy++) {
       using(var repeated=(GraphicsPath)path.Clone()) {
        using(var m=new Matrix(scale,0,0,scale,cursor-b.Left*scale,row*64+8-minY*scale)) repeated.Transform(m);
        g.FillPath(Brushes.White,repeated);
       }
       cursor+=textWidth;
       if(copy<repeats-1) {
        Pattern(g,cursor+32,cursor+32+symbolWidth,row*64+8,kind,reverse);
        cursor+=gap;
       }
      }
      g.ResetClip();
      continue;
     }
     float total=textWidth+48+184;
     if(total>2016) throw new Exception("Row overflows");
     float start=(2048-total)/2;
     int mode=row%8;
     if(variation) {
      start=(2048-textWidth)/2;
      if(mode==1 || mode==5) start=48;
      if(mode==3) start=2048-48-textWidth;
     }
     using(var m=new Matrix(scale,0,0,scale,start-b.Left*scale,row*64+8-minY*scale)) path.Transform(m);
     g.SetClip(new Rectangle(0,row*64+8,2048,48));
     g.FillPath(Brushes.White,path);
     float arrowX=start+textWidth+48;
     if(variation) {
      int kind=(mode==2 || mode==6) ? 1 : 0;
      bool reverse=(mode==3 || mode==7);
      Pattern(g,32,start-40,row*64+8,kind,reverse);
      Pattern(g,start+textWidth+40,2016,row*64+8,kind,reverse);
     } else for(int j=0;j<4;j++) {
      float x=arrowX+j*48, y=row*64+8;
      g.FillPolygon(Brushes.White,new PointF[]{new PointF(x,y),new PointF(x+18,y),new PointF(x+40,y+24),new PointF(x+18,y+48),new PointF(x,y+48),new PointF(x+22,y+24)});
     }
     g.ResetClip();
    }
   }
   for(int row=0;row<32;row++) {
    int top=2048,bottom=-1;
    for(int y=row*64;y<(row+1)*64;y++) {
     bool ink=false;
     for(int x=0;x<2048;x++) { if(bitmap.GetPixel(x,y).R>0) {ink=true;break;} }
     if(ink) {top=Math.Min(top,y);bottom=y;}
    }
    bool textOnly=mixedText && row%3==0;
    if(textOnly ? (top<row*64+8 || bottom>row*64+55 || bottom<top) : (top!=row*64+8 || bottom!=row*64+55)) throw new Exception("Row bounds invalid: "+row+" "+top+" "+bottom);
   }
   bitmap.Save(output,ImageFormat.Png);
  }
  foreach(var p in paths) p.Dispose(); family.Dispose(); format.Dispose();
  Console.WriteLine("Verified: 2048x2048, 32 rows at 64px, ink confined to 48px band, 8px safe top/bottom padding, common font transform and baseline.");
  if(mixedText) Console.WriteLine("Five phrases only. Layouts: 11 text-only rows, 11 single-text plus chevrons rows, 10 double-text plus chevrons rows.");
 }
}
'@
$atlasName = if ($MixedText) { 'text-atlas-2048-32x64-five-phrases-mixed.png' } elseif ($RepeatText) { 'text-atlas-2048-32x64-repeated.png' } elseif ($Variation) { 'text-atlas-2048-32x64-variation.png' } else { 'text-atlas-2048-32x64.png' }
[WarningAtlas]::Build((Join-Path $PSScriptRoot 'text-atlas-32-rows-prompt.txt'), (Join-Path $PSScriptRoot $atlasName), $Variation.IsPresent, $RepeatText.IsPresent, $MixedText.IsPresent)
