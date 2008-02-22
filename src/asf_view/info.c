#include "asf_view.h"
#include "libasf_proj.h"

static void line_samp_to_proj(ImageInfo *ii, double line, double samp,
                              double *x, double *y)
{
  meta_parameters *meta = ii->meta;
  if (meta->projection || (meta->sar&&meta->state_vectors) || meta->transform)
  {
    double lat, lon, projZ;
    meta_get_latLon(meta, line, samp, 0, &lat, &lon);
    if (meta->projection &&
        meta->projection->type != LAT_LONG_PSEUDO_PROJECTION)
    {
      latlon_to_proj(meta->projection, 'R', lat*D2R, lon*D2R, 0,
                     x, y, &projZ);
    }
    else {
      latLon2UTM(lat, lon, 0, x, y);
    }
  } else {
    *x = samp;
    *y = line;
  }
}

void update_pixel_info(ImageInfo *ii)
{
    // update the left-hand "clicked pixel" information
    char buf[512];

    GtkWidget *img = get_widget_checked("big_image");
    GdkPixbuf *shown_pixbuf = gtk_image_get_pixbuf(GTK_IMAGE(img));

    double x = crosshair_samp;
    double y = crosshair_line;
    int nl = ii->meta->general->line_count;
    int ns = ii->meta->general->sample_count;
    CachedImage *data_ci = ii->data_ci;
    meta_parameters *meta = ii->meta;

    sprintf(buf, "Line: %.1f, Sample: %.1f\n", y, x);

    if (x < 0 || x >= ns || y < 0 || y >= nl)
    {
        // outside of the image
        sprintf(&buf[strlen(buf)], "Pixel Value: (outside image)\n");
    }
    else
    {
        assert(meta);
        assert(shown_pixbuf);

        if (data_ci->data_type == GREYSCALE_FLOAT) {
            float fval = cached_image_get_pixel(data_ci,
                crosshair_line, crosshair_samp);
            int uval = calc_scaled_pixel_value(&(ii->stats), fval);

            sprintf(&buf[strlen(buf)], "Pixel Value: %f --> %d\n",
                fval, uval);
        }
        else if (data_ci->data_type == RGB_BYTE) {
            unsigned char r, g, b;
            cached_image_get_rgb(data_ci, crosshair_line, crosshair_samp,
                &r, &g, &b);
            sprintf(&buf[strlen(buf)], "Pixel Value: R,G,B = %d, %d, %d\n",
                (int)r, (int)g, (int)b);
        }
        else if (data_ci->data_type == GREYSCALE_BYTE) {
            unsigned char r, g, b;
            cached_image_get_rgb(data_ci, crosshair_line, crosshair_samp,
                &r, &g, &b);
            sprintf(&buf[strlen(buf)], "Pixel Value: %d\n", (int)r);
        }
        else if (data_ci->data_type == RGB_FLOAT) {
            unsigned char r, g, b;
            float rf, gf, bf;
            cached_image_get_rgb(data_ci, crosshair_line, crosshair_samp,
                &r, &g, &b);
            cached_image_get_rgb_float(data_ci, crosshair_line, crosshair_samp,
                &rf, &gf, &bf);
            sprintf(&buf[strlen(buf)],  "Red: %f -> %d\n"
                                        "Green: %f -> %d\n"
                                        "Blue: %f -> %d\n",
                rf, (int)r, gf, (int)g, bf, (int)b);
        }
    }

    double lat=0, lon=0;
    if (meta->projection || (meta->sar&&meta->state_vectors) ||
        meta->transform || meta->airsar)
    {
        meta_get_latLon(meta, y, x, 0, &lat, &lon);
        sprintf(&buf[strlen(buf)], "Lat: %.4f, Lon: %.4f (deg)\n", lat, lon);

        //double px, py;
        //latLon2UTM(lat,lon,0,&px,&py);
        //printf("%7.2f %7.2f --> %13.2f %13.2f\n", lat, lon, px, py);
    }

    // skip projection coords if not projected, or lat/long pseudo (since
    // in that case the projection coords are just the lat/long values
    // we are already showing)
    if (meta->projection &&
        meta->projection->type != LAT_LONG_PSEUDO_PROJECTION)
    {
        double projX, projY, projZ;
        latlon_to_proj(meta->projection, 'R', lat*D2R, lon*D2R, 0,
            &projX, &projY, &projZ);
        sprintf(&buf[strlen(buf)], "Proj X,Y: %.1f, %.1f m\n",
            projX, projY);
    }

    if (meta->state_vectors && meta->sar) {
        double s,t;
        meta_get_timeSlantDop(meta, y, x, &t, &s, NULL);
        sprintf(&buf[strlen(buf)],
            "Incid: %.4f, Look: %.4f (deg)\n"
            "Slant: %.1f m Time: %.3f s\n",
            R2D*meta_incid(meta,y,x), R2D*meta_look(meta,y,x), s, t);
    }

    if (g_poly->n > 0) {
        // start distance measure at crosshair coords
        double cross_x, cross_y, prev_x, prev_y;
        line_samp_to_proj(ii, y, x, &cross_x, &cross_y);
        prev_x = cross_x; prev_y = cross_y;

        // iterate through ctrl-clicked coords
        int i;
        double d=0, A=0; // d=distance, A=area
        for (i=0; i<g_poly->n; ++i) {
            double proj_x, proj_y;       
            line_samp_to_proj(ii, g_poly->line[i], g_poly->samp[i],
                              &proj_x, &proj_y);

            d += hypot(proj_x-prev_x, proj_y-prev_y);
            A += prev_x * proj_y - proj_x * prev_y;

            prev_x = proj_x; prev_y = proj_y;

            // for the area calc, we close the polygon automatically
            if (i==g_poly->n-1)
                A += prev_x * cross_y - cross_x * prev_y;
        }
        A /= 2.;

        char *units = "m";
        if (!meta->sar && !meta->transform && !meta->projection)
            units = "pixels";

        if (g_poly->n == 1)
            sprintf(&buf[strlen(buf)], "Distance to %.1f,%.1f: %.1f %s",
                g_poly->line[0], g_poly->samp[0], d, units);
        else
            sprintf(&buf[strlen(buf)],
                "Total distance: %.1f %s (%d points)\n"
                "Area (of closure): %.1f %s^2",
                d, units, g_poly->n+1, fabs(A), units);
    } else {
        sprintf(&buf[strlen(buf)], "Distance: (ctrl-click to measure)");
    }

    put_text_in_textview(buf, "info_textview");
    //GtkWidget *lbl = get_widget_checked("upper_label");
    //gtk_label_set_text(GTK_LABEL(lbl), buf);
}
