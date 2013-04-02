//extern AGG-based DRAW functions

extern REBINT Draw_Image(REBYTE *image, REBINT w, REBINT h, REBSER *block);
extern void agg_get_size(void* gr, REBPAR* p);

extern void agg_add_vertex (void* gr, REBXYF p);
extern void agg_anti_alias(void* gr, REBINT mode);
extern void agg_arc(void* gr, REBXYF c, REBXYF r, REBDEC ang1, REBDEC ang2, REBINT closed);
extern void agg_arrow(void* gr, REBXYF mode, REBCNT col);
extern void agg_begin_poly (void* gr, REBXYF p);
extern void agg_box(void* gr, REBXYF p1, REBXYF p2, REBDEC r);
extern void agg_circle(void* gr, REBXYF p, REBXYF r);
extern void agg_clip(void* gr, REBXYF p1, REBXYF p2);
extern void agg_curve3(void* gr, REBXYF p1, REBXYF p2, REBXYF p3);
extern void agg_curve4(void* gr, REBXYF p1, REBXYF p2, REBXYF p3, REBXYF p4);
extern void agg_ellipse(void* gr, REBXYF p1, REBXYF p2);
extern void agg_end_poly (void* gr);
extern void agg_end_spline (void* gr, REBINT step, REBINT closed);
extern void agg_fill_pen(void* gr, REBCNT color);
extern void agg_fill_pen_image(void* gr, REBYTE* img, REBINT w, REBINT h);
extern void agg_fill_rule(void* gr, REBINT mode);
extern void agg_gamma(void* gr, REBDEC gamma);
extern void agg_gradient_pen(void* gr, REBINT gradtype, REBINT mode, REBXYF oft, REBXYF range, REBDEC angle, REBXYF scale, REBSER* colors); //REBDEC begin, REBDEC end, REBDEC ang, REBDEC scX,REBDEC scY, REBSER* colors); //unsigned char *colors, REBDEC* offsets);
extern void agg_invert_matrix(void* gr);
extern void agg_image(void* gr, REBYTE* img, REBINT w, REBINT h,REBXYF offset);
extern void agg_image_filter(void* gr, REBINT type, REBINT mode, REBDEC blur);
extern void agg_image_options(void* gr, REBCNT keyCol, REBINT border);
extern void agg_image_scale(void* gr, REBYTE* img, REBINT w, REBINT h, REBSER* points);
extern void agg_image_pattern(void* gr, REBINT mode, REBXYF offset, REBXYF size);
extern void agg_line(void* gr, REBXYF p1, REBXYF p2);
extern void agg_line_cap(void* gr, REBINT mode);
extern void agg_line_join(void* gr, REBINT mode);
extern void agg_line_pattern(void* gr, REBCNT col, REBDEC* patterns);
extern void agg_line_width(void* gr, REBDEC width, REBINT mode);
extern void agg_matrix(void* gr, REBSER* mtx);
extern void agg_pen(void* gr, REBCNT col);
extern void agg_pen_image(void* gr, REBYTE* img, REBINT w, REBINT h);
extern void agg_pop_matrix(void* gr);
extern void agg_push_matrix(void* gr);
extern void agg_reset_gradient_pen(void* gr);
extern void agg_reset_matrix(void* gr);
extern void agg_rotate(void* gr, REBDEC ang);
extern void agg_scale(void* gr, REBXYF sc);
extern void agg_skew(void* gr, REBXYF angle);
extern REBINT agg_text(void* gr, REBINT mode, REBXYF* p1, REBXYF* p2, REBSER* block);
extern void agg_transform(void* gr, REBDEC ang, REBXYF ctr, REBXYF scm, REBXYF oft);
extern void agg_translate(void* gr, REBXYF p);
extern void agg_triangle(void* gr, REBXYF p1, REBXYF p2, REBXYF p3, REBCNT c1, REBCNT c2, REBCNT c3, REBDEC dilation);


extern void agg_effect (void* gr, REBPAR* p1, REBPAR* p2, REBSER* block);



//SHAPE functions
extern void agg_path_arc(void* gr, REBINT rel, REBXYF p, REBXYF r, REBDEC ang, REBINT sweep, REBINT large);
extern void agg_path_close(void* gr);
extern void agg_path_hline(void* gr,REBINT rel, REBDEC x);
extern void agg_path_line(void* gr, REBINT rel, REBXYF p);
extern void agg_path_move(void* gr, REBINT rel, REBXYF p);
extern void agg_path_open(void* gr);
extern void agg_path_vline(void* gr,REBINT rel, REBDEC y);
extern void agg_path_curv(void* gr, REBINT rel, REBXYF p1, REBXYF p2);
extern void agg_path_curve(void* gr, REBINT rel, REBXYF p1, REBXYF p2, REBXYF p3);
extern void agg_path_qcurv(void* gr, REBINT rel, REBXYF p);
extern void agg_path_qcurve(void* gr, REBINT rel, REBXYF p1, REBXYF p2);
