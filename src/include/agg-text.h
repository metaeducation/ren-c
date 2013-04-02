//typedef struct REBOL_FONT REBFNT;
typedef struct REBOL_FONT {
	wchar_t *name;
	REBCNT name_free;
	REBINT bold;
	REBINT italic;
	REBINT underline;
	REBINT size;
	REBYTE* color;
	REBINT offset_x;
	REBINT offset_y;
	REBINT space_x;
	REBINT space_y;
	REBINT shadow_x;
	REBINT shadow_y;
	REBYTE* shadow_color;
	REBINT shadow_blur;
} REBFNT;

//typedef struct REBOL_PARA REBPRA;
typedef struct REBOL_PARA {
		REBINT origin_x;
		REBINT origin_y;
		REBINT margin_x;
		REBINT margin_y;
		REBINT indent_x;
		REBINT indent_y;
		REBINT tabs;
		REBINT wrap;
		float scroll_x;
		float scroll_y;
		REBINT align;
		REBINT valign;
} REBPRA;

//extern AGG-based compositor functions

extern void* Create_RichText();
extern void Destroy_RichText(void* rt);
extern void rt_anti_alias(void* rt, REBINT mode);
extern void rt_bold(void* rt, REBINT state);
extern void rt_caret(void* rt, REBXYF* caret, REBXYF* highlightStart, REBXYF highlightEnd);
extern void rt_center(void* rt);
extern void rt_color(void* rt, REBCNT col);
extern void rt_drop(void* rt, REBINT number);
extern void rt_font(void* rt, REBFNT* font);
extern void rt_font_size(void* rt, REBINT size);
extern void* rt_get_font(void* rt);
extern void* rt_get_para(void* rt);
extern void rt_italic(void* rt, REBINT state);
extern void rt_left(void* rt);
extern void rt_newline(void* rt, REBINT index);
extern void rt_para(void* rt, REBPRA* para);
extern void rt_right(void* rt);
extern void rt_scroll(void* rt, REBXYF offset);
extern void rt_shadow(void* rt, REBXYF d, REBCNT color, REBINT blur);
extern void rt_set_font_styles(REBFNT* font, u32 word);
extern void rt_size_text(void* rt, REBGOB* gob, REBXYF* size);
extern void rt_text(void* gr, REBCHR* text, REBINT index, REBCNT gc);
extern void rt_underline(void* rt, REBINT state);

extern REBINT rt_offset_to_caret(void* rt, REBGOB *gob, REBXYF xy, REBINT *element, REBINT *position);
extern REBINT rt_caret_to_offset(void* rt, REBGOB *gob, REBXYF* xy, REBINT element, REBINT position);



