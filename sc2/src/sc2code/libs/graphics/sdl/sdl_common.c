//Copyright Paul Reiche, Fred Ford. 1992-2002

/*
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#ifdef GFXMODULE_SDL

#include "sdl_common.h"
#include "opengl.h"
#include "pure.h"
#include "rotozoom.h"
#include "primitives.h"
#include "dcqueue.h"
#include "SDL_thread.h"

SDL_Surface *SDL_Video;
SDL_Surface *SDL_Screen;
SDL_Surface *ExtraScreen;

BOOLEAN ShowFPS = FALSE;
volatile int continuity_break;


int
TFB_InitGraphics (int driver, int flags, int width, int height, int bpp)
{
	int result;

	if (driver == TFB_GFXDRIVER_SDL_OPENGL)
	{
#ifdef HAVE_OPENGL
		result = TFB_GL_InitGraphics (driver, flags, width, height, bpp);
#else
		driver = TFB_GFXDRIVER_SDL_PURE;
		printf("OpenGL support not compiled in, so using pure sdl driver\n");
		result = TFB_Pure_InitGraphics (driver, flags, width, height, bpp);
#endif
	}
	else
	{
		result = TFB_Pure_InitGraphics (driver, flags, width, height, bpp);
	}

	SDL_EnableUNICODE (1);
	SDL_WM_SetCaption (TFB_WINDOW_CAPTION, NULL);

	if (flags & TFB_GFXFLAGS_SHOWFPS)
		ShowFPS = TRUE;

	//if (flags & TFB_GFXFLAGS_FULLSCREEN)
	//	SDL_ShowCursor (SDL_DISABLE);

	return 0;
}

int
TFB_CreateGamePlayThread ()
{
	TFB_DEBUG_HALT = 0;

	GraphicsSem = SDL_CreateSemaphore (1);
	_MemorySem = SDL_CreateSemaphore (1);

	SDL_CreateThread (Starcon2Main, NULL);

	return 0;
}

void
TFB_ProcessEvents ()
{
	SDL_Event Event;

	while (SDL_PollEvent (&Event))
	{
		switch (Event.type) {
			case SDL_ACTIVEEVENT:    /* Loose/gain visibility */
				// TODO
				break;
			case SDL_KEYDOWN:        /* Key pressed */
			case SDL_KEYUP:          /* Key released */
				ProcessKeyboardEvent (&Event);
				break;
			case SDL_JOYAXISMOTION:  /* Joystick axis motion */
			case SDL_JOYBUTTONDOWN:  /* Joystick button pressed */
			case SDL_JOYBUTTONUP:    /* Joystick button released */
				ProcessJoystickEvent (&Event);
				break;
			case SDL_QUIT:
				exit (0);
				break;
			case SDL_VIDEORESIZE:    /* User resized video mode */
				// TODO
				break;
			case SDL_VIDEOEXPOSE:    /* Screen needs to be redrawn */
				TFB_SwapBuffers ();
				break;
			default:
				break;
		}

		//Still testing this... Should fix sticky input soon...
		/*
		{
			Uint8* Temp;

			SDL_PumpEvents ();
			Temp = SDL_GetKeyState (NULL);
			for(a = 0; a < 512; a++)
			{
				KeyboardDown[a] = Temp[a];
			}
		}
		*/
	}
}

TFB_Image* 
TFB_LoadImage (SDL_Surface *img)
{
	TFB_Image *myImage;

	myImage = (TFB_Image*) HMalloc (sizeof (TFB_Image));
	myImage->mutex = CreateMutex();
	myImage->ScaledImg = NULL;
	myImage->Palette = NULL;
	
	if (img->format->BytesPerPixel == 1)
	{
		int x,y;
		Uint8 *src_p,*dst_p;
		SDL_Surface *full_surf;
		SDL_Rect SDL_DstRect;

		full_surf = SDL_CreateRGBSurface (SDL_SWSURFACE, img->clip_rect.w, img->clip_rect.h, 
			8, 0, 0, 0, 0);

		SDL_DstRect.x = SDL_DstRect.y = 0;

		SDL_LockSurface(img);
		SDL_LockSurface(full_surf);

		src_p = (Uint8*)img->pixels;
		dst_p = (Uint8*)full_surf->pixels;
	
		for (y = img->clip_rect.y; y < img->clip_rect.y + img->clip_rect.h; ++y)
		{
			for (x = img->clip_rect.x; x < img->clip_rect.x + img->clip_rect.w; ++x)
			{
				dst_p[(y - img->clip_rect.y) * full_surf->pitch + (x - img->clip_rect.x)]=
					src_p[y * img->pitch + x];
			}
		}

		full_surf->clip_rect.x = full_surf->clip_rect.y = 0;
		full_surf->clip_rect.w = img->clip_rect.w;
	    full_surf->clip_rect.h = img->clip_rect.h;
		
		myImage->Palette = (SDL_Color*) HMalloc (sizeof (SDL_Color) * 256);

		for (x = 0; x < 256; ++x)
		{
			myImage->Palette[x].r = img->format->palette->colors[x].r;
			myImage->Palette[x].g = img->format->palette->colors[x].g;
			myImage->Palette[x].b = img->format->palette->colors[x].b;
		}

		SDL_SetColors (full_surf, myImage->Palette, 0, 256);

		if (img->flags & SDL_SRCCOLORKEY)
		{
			SDL_SetColorKey (full_surf, SDL_SRCCOLORKEY, img->format->colorkey);
		}

		SDL_UnlockSurface(full_surf);
		SDL_UnlockSurface(img);

		SDL_FreeSurface (img);
		img = full_surf;
	}
	else if (img->format->BytesPerPixel == 2 || img->format->BytesPerPixel == 3) {
		SDL_Surface *full_surf;
		SDL_Rect SDL_DstRect;

		full_surf = SDL_CreateRGBSurface (SDL_SWSURFACE, img->clip_rect.w, img->clip_rect.h, 
			32, 0x000000FF, 0x0000FF00, 0x00FF0000, 0xFF000000);

		SDL_DstRect.x = SDL_DstRect.y = 0;

		SDL_BlitSurface (img, &img->clip_rect, full_surf, &SDL_DstRect);

		full_surf->clip_rect.x = full_surf->clip_rect.y = 0;
		full_surf->clip_rect.w = img->clip_rect.w;
	    full_surf->clip_rect.h = img->clip_rect.h;
		
		SDL_FreeSurface (img);
		img = full_surf;
	}

	if (img->format->BytesPerPixel > 1)
	{
		myImage->NormalImg = TFB_DisplayFormatAlpha (img);
		if (myImage->NormalImg)
		{
			SDL_FreeSurface(img);
		}
		else
		{
			printf("TFB_LoadImage(): TFB_DisplayFormatAlpha failed\n");
			myImage->NormalImg = img;
		}
	}
	else
	{
		myImage->NormalImg = img;
	}

	return(myImage);
}

void
TFB_FreeImage (TFB_Image *img)
{
	if (img)
	{
		TFB_DrawCommand DC;

		DC.Type = TFB_DRAWCOMMANDTYPE_DELETEIMAGE;
		DC.image = (TFB_ImageStruct*) img;

		TFB_EnqueueDrawCommand (&DC);
	}
}

void
TFB_SwapBuffers ()
{
#ifdef HAVE_OPENGL
	if (GraphicsDriver == TFB_GFXDRIVER_SDL_OPENGL)
		TFB_GL_SwapBuffers ();
	else
		TFB_Pure_SwapBuffers ();
#else
	TFB_Pure_SwapBuffers ();
#endif
}

SDL_Surface* 
TFB_DisplayFormatAlpha (SDL_Surface *surface)
{
#ifdef HAVE_OPENGL
	if (GraphicsDriver == TFB_GFXDRIVER_SDL_OPENGL)
	{
		return TFB_GL_DisplayFormatAlpha (surface);
	}
#endif

	return SDL_DisplayFormatAlpha (surface);
}

void
TFB_ComputeFPS ()
{
	static Uint32 last_time = 0, fps_counter = 0;
	Uint32 current_time, delta_time;

	current_time = SDL_GetTicks ();
	delta_time = current_time - last_time;
	last_time = current_time;
	
	fps_counter += delta_time;
	if (fps_counter > 1000)
	{
		fps_counter = 0;
		printf ("fps %.2f\n",1.0 / (delta_time / 1000.0));
	}
}

// Livelock deterrance constants.  Because the entire screen is rarely
// refreshed, we may not drop draw commands on the floor with abandon.
// Furthermore, if the main program is queuing commands at a speed
// comparable to our processing of the commands, we never finish and
// the game freezes.  Thus, if the queue starts out larger than
// DCQ_FORCE_SLOWDOWN_SIZE, or DCQ_LIVELOCK_MAX commands find
// themselves being processed in one go, livelock deterrence is
// enabled, and TFB_FlushGraphics locks the DCQ until it has processed
// all entries.  If batched but pending commands exceed DCQ_FORCE_BREAK_SIZE,
// a continuity break is performed.  This will effectively slow down the 
// game logic, a fate we seek to avoid - however, it seems to be unavoidable
// on slower machines.  Even there, it's seems nonexistent outside of
// communications screens.  --Michael

#define DCQ_FORCE_SLOWDOWN_SIZE 1024
#define DCQ_FORCE_BREAK_SIZE 4096
#define DCQ_LIVELOCK_MAX 2048

void
TFB_FlushGraphics () // Only call from main thread!!
{
	int semval;
	int commands_handled;
	BOOLEAN livelock_deterrence;

	// This is technically a locking violation on DrawCommandQueue->Size,
	// but it is likely to not be very destructive.
	if (DrawCommandQueue == 0 || DrawCommandQueue->Size == 0)
	{
		static int last_fade = 255;
		int current_fade = TFB_GetFadeAmount();			
		
		if (current_fade != 255 && current_fade != last_fade)
			TFB_SwapBuffers(); // if fading, redraw every frame
		last_fade = current_fade;

		SDL_Delay(5);
		return;
	}

	if (!continuity_break) {
		// TODO: a more optimal way of getting the lock on GraphicsSem..
		//       cannot currently use SDL_SemWait because it would break
		//       the usage of continuity_break
		// Michael asks: Why are we locking, then releasing, the GraphicsSem?

		semval = TimeoutSetSemaphore (GraphicsSem, ONE_SECOND / 10);
		if (semval != 0)
			return;
		else
			SDL_SemPost (GraphicsSem);
	}
	continuity_break = 0;

	if (ShowFPS)
		TFB_ComputeFPS ();

	commands_handled = 0;
	livelock_deterrence = FALSE;

	if (DrawCommandQueue->FullSize > DCQ_FORCE_BREAK_SIZE)
	{
		TFB_BatchReset ();
	}

	if (DrawCommandQueue->Size > DCQ_FORCE_SLOWDOWN_SIZE)
	{
		Lock_DCQ ();
		livelock_deterrence = TRUE;
	}

	while (TRUE)
	{
		TFB_DrawCommand DC;
		TFB_Image *DC_image;

		if (!TFB_DrawCommandQueue_Pop (DrawCommandQueue, &DC))
		{
			// the Queue is now empty.
			break;
		}

		++commands_handled;
		if (!livelock_deterrence && commands_handled + DrawCommandQueue->Size > DCQ_LIVELOCK_MAX)
		{
			// printf("Initiating livelock deterrence!\n");
			livelock_deterrence = TRUE;
			
			Lock_DCQ ();
		}

		DC_image = (TFB_Image*) DC.image;
		if (DC_image)
			LockMutex (DC_image->mutex);
		
		switch (DC.Type)
		{
		case TFB_DRAWCOMMANDTYPE_IMAGE:
			{
				SDL_Rect targetRect;
				SDL_Surface *surf;

				if (DC_image == 0)
				{
					printf ("TFB_FlushGraphics(): error, DC_image == 0\n");
					break;
				}

				targetRect.x = DC.x;
				targetRect.y = DC.y;

				if ((DC.w != DC_image->NormalImg->w || DC.h != DC_image->NormalImg->h) && 
					DC_image->ScaledImg)
					surf = DC_image->ScaledImg;
				else
					surf = DC_image->NormalImg;

				if (surf->format->BytesPerPixel == 1)
				{
					if (DC.UsePalette)
					{
						SDL_SetColors (surf, (SDL_Color*)DC.Palette, 0, 256);
					}
					else
					{
						SDL_SetColors (surf, DC_image->Palette, 0, 256);
					}
				}

				SDL_BlitSurface(surf, NULL, SDL_Screen, &targetRect);

				break;
			}
		case TFB_DRAWCOMMANDTYPE_LINE:
			{
				SDL_Rect r;
				int x1, y1, x2, y2;
				Uint32 color;
				PutPixelFn screen_plot;

				screen_plot = putpixel_for (SDL_Screen);
				color = SDL_MapRGB (SDL_Screen->format, DC.r, DC.g, DC.b);

				x1 = DC.x;
				x2 = DC.w;
				y1 = DC.y;
				y2 = DC.h;
				
				SDL_GetClipRect(SDL_Screen, &r);
				
				if (x1 < r.x)
					x1 = r.x;
				else if (x1 > r.x + r.w)
					x1 = r.x + r.w;

				if (x2 < r.x)
					x2 = r.x;
				else if (x2 > r.x + r.w)
					x2 = r.x + r.w;
				
				if (y1 < r.y)
					y1 = r.y;
				else if (y1 > r.y + r.h)
					y1 = r.y + r.h;

				if (y2 < r.y)
					y2 = r.y;
				else if (y2 > r.y + r.h)
					y2 = r.y + r.h;

				SDL_LockSurface (SDL_Screen);
				line (x1, y1, x2, y2, color, screen_plot, SDL_Screen);
				SDL_UnlockSurface (SDL_Screen);

				break;
			}
			case TFB_DRAWCOMMANDTYPE_RECTANGLE:
			{
				SDL_Rect r;
				r.x = DC.x;
				r.y = DC.y;
				r.w = DC.w;
				r.h = DC.h;

				SDL_FillRect(SDL_Screen, &r, SDL_MapRGB(SDL_Screen->format, DC.r, DC.g, DC.b));
				break;
			}
		case TFB_DRAWCOMMANDTYPE_SCISSORENABLE:
			{
				SDL_Rect r;
				r.x = DC.x;
				r.y = DC.y;
				r.w = DC.w;
				r.h = DC.h;
				
				SDL_SetClipRect(SDL_Screen, &r);
				break;
			}
		case TFB_DRAWCOMMANDTYPE_SCISSORDISABLE:
			SDL_SetClipRect(SDL_Screen, NULL);
			break;
		case TFB_DRAWCOMMANDTYPE_COPYBACKBUFFERTOOTHERBUFFER:
			{
				SDL_Rect src, dest;
				src.x = dest.x = DC.x;
				src.y = dest.y = DC.y;
				src.w = DC.w;
				src.h = DC.h;

				if (DC_image == 0) 
				{
					SDL_BlitSurface(SDL_Screen, &src, ExtraScreen, &dest);
				}
				else
				{
					dest.x = 0;
					dest.y = 0;
					SDL_BlitSurface(SDL_Screen, &src, DC_image->NormalImg, &dest);
				}
				break;
			}
		case TFB_DRAWCOMMANDTYPE_COPYFROMOTHERBUFFER:
			{
				SDL_Rect src, dest;
				src.x = dest.x = DC.x;
				src.y = dest.y = DC.y;
				src.w = DC.w;
				src.h = DC.h;
				SDL_BlitSurface(ExtraScreen, &src, SDL_Screen, &dest);
				break;
			}
		case TFB_DRAWCOMMANDTYPE_DELETEIMAGE:
			SDL_FreeSurface (DC_image->NormalImg);
			
			if (DC_image->ScaledImg) {
				//printf("DELETEIMAGE to ScaledImg %x, size %d %d\n",DC_image->ScaledImg,DC_image->ScaledImg->w,DC_image->ScaledImg->h);
				SDL_FreeSurface (DC_image->ScaledImg);
			}

			if (DC_image->Palette)
				HFree (DC_image->Palette);

			UnlockMutex (DC_image->mutex);
			DestroyMutex (DC_image->mutex);
			
			HFree (DC_image);
			DC_image = 0;
			break;
		}
		
		if (DC_image)
			UnlockMutex (DC_image->mutex);
	}
	
	if (livelock_deterrence)
	{
		Unlock_DCQ ();
	}

	TFB_SwapBuffers();
}

#endif
