#pragma once

/* Internal interface */

typedef BRUSH PEN, *PPEN;

PPEN
NTAPI
PEN_AllocPenWithHandle(
    VOID);

PPEN
NTAPI
PEN_AllocExtPenWithHandle(
    VOID);

#define PEN_UnlockPen(pPenObj) GDIOBJ_vUnlockObject((POBJ)pPenObj)
#define PEN_ShareUnlockPen(ppen) GDIOBJ_vDereferenceObject((POBJ)ppen)

PPEN
FASTCALL
PEN_ShareLockPen(HPEN hpen);

INT
NTAPI
PEN_GetObject(
    _In_ PPEN pPen,
    _In_ INT Count,
    _Out_ PLOGPEN Buffer);

VOID FASTCALL AddPenLinesBounds(PDC,int,POINT *);

/* A pen is stroked through the wide-pen region path when it is wider than a
 * single pixel, OR when it is a geometric pen whose fill is a pattern/hatch
 * brush.  The latter is required even at width 1 because Windows fills a
 * brushed geometric pen with its brush pattern, whereas the thin single-pixel
 * line path would draw it with the (solid) realized line colour instead. */
#define IntIsEffectiveWidePen(pbrLine) ( \
    (((pbrLine->flAttrs & BR_IS_OLDSTYLEPEN) || \
      ((pbrLine)->ulPenStyle & PS_TYPE_MASK) == PS_GEOMETRIC)) && \
    ((pbrLine)->lWidth > 1 || \
     ((pbrLine)->flAttrs & (BR_IS_BITMAP | BR_IS_DIB | BR_IS_HATCH))) \
)
