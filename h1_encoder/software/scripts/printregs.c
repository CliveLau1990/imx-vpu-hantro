#include "encswhwregisters.h"
#include <stdio.h>

int main(void)
{
 int i,j;
 unsigned int lsb,msb,mask;
 const regField_s *field;

 
 printf("register name;offset;msb;lsb;rw;description\n");
 for (i=0;i<HEncRegisterAmount;i++)
 {
   field = &asicRegisterDesc[i];
   mask = (field->mask>>field->lsb); 
   msb = field->lsb;
   lsb = field->lsb;
   
   while (mask!=0)
   {
    mask=mask>>1;
    msb++;
   }
   msb--;

   printf("swreg%d; 0x%03x; %2d; %2d; %s; %s\n",
           field->base/4, field->base, msb, lsb,
           (field->rw == RO) ? "RO" : (field->rw == WO) ? "WO" : "RW", field->description); 
      
 }


 return 0;
}
