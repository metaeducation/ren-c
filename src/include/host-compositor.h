//compositor API functions

extern void* rebcmp_create(REBGOB* rootGob, REBGOB* gob);

extern void rebcmp_destroy(void* context);

//extern REBSER* Gob_To_Image(REBGOB *gob);

extern void rebcmp_compose(void* context, REBGOB* winGob, REBGOB* gob);

extern void rebcmp_blit(void* context);

extern REBYTE* rebcmp_get_buffer(void* context);

extern void rebcmp_release_buffer(void* context);

extern REBOOL rebcmp_resize_buffer(void* context, REBGOB* winGob);

//extern REBINT Draw_Image(REBSER *image, REBSER *block);

//extern REBINT Effect_Image(REBSER *image, REBSER *block);
