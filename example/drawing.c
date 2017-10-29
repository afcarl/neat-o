#include <assert.h>
#include <math.h>

#include <gtk/gtk.h>
#include <nn.h>
#include <neat.h>

#define POP_SIZE 12

static struct neat_config config = {
	.network_inputs = 2,
	.network_outputs = 1,
	.network_hidden_nodes = 3,

	.minimum_time_before_replacement = 10,

	.population_size = POP_SIZE,

	.speciate = true,

	.species_crossover_probability = 0.2,
	.interspecies_crossover_probability = 0.05,
	.mutate_species_crossover_probability = 0.5,

	.genome_add_neuron_mutation_probability = 0.1,
	.genome_add_link_mutation_probability = 0.12,
	.genome_weight_mutation_probability = 0.5,
	.genome_all_weights_mutation_probability = 0.21,

	.genome_minimum_ticks_alive = 100,
	.genome_compatibility_treshold = 0.2
};

static neat_t neat = NULL;

static float xor_inputs[4][2] = {
	{0.0f, 0.0f},
	{0.0f, 1.0f},
	{1.0f, 0.0f},
	{1.0f, 1.0f}
};
static float xor_outputs[4] = {0.0f, 1.0f, 1.0f, 0.0f};

/* Demo specific declarations */
static guint thread;
static cairo_surface_t *surface = NULL;

static float errors[POP_SIZE];
static size_t frame = 0;
static size_t worst = SSIZE_MAX;
static guint renderx = 1;
static guint rendery = 1;
static guint rendertick = 0;

static void setup_neat()
{
	neat = neat_create(config);
	assert(neat);
}

static gboolean tick(gpointer data)
{
	for(size_t i = 0; i < config.population_size; i++){
		float error = 0.0f;
		for(int k = 0; k < 4; k++){
			const float *results = neat_run(neat, i, xor_inputs[k]);

			error += fabs(results[0] - xor_outputs[k]);
		}

		errors[i] = error;
		float fitness = (4.0 - error) / 4.0;
		neat_set_fitness(neat, i, fitness * fitness);

		neat_increase_time_alive(neat, i);
	}

	neat_epoch(neat, &worst);

	frame++;

	/* The amount of cycles before each network is rendered is decided
	 * by the size of the network because Cairo is slow at rendering
	 * a lot of lines
	 */
	if(++rendertick > renderx * rendery){
		rendertick = 0;
		gtk_widget_queue_draw(GTK_WIDGET(data));
	}

	return TRUE;
}

static void draw_neuron(cairo_t *cr,
			guint x,
			guint y,
			int radius,
			float value,
			bool is_bias,
			enum nn_activation activation)
{
	cairo_save(cr);
	if(is_bias){
		radius /= 1.5;
		cairo_arc(cr, x, y, radius, 0, 2 * G_PI);
	}else{
		switch(activation){
			case NN_ACTIVATION_PASSTHROUGH:
				cairo_move_to(cr, x - radius, y - radius);
				cairo_line_to(cr, x - radius, y + radius);
				cairo_line_to(cr, x + radius, y);
				cairo_close_path(cr);
				break;
			case NN_ACTIVATION_SIGMOID:
				cairo_rectangle(cr,
						x - radius / 2,
						y - radius / 2,
						radius,
						radius);
				break;
			default:
				cairo_arc(cr, x, y, radius, 0, 2 * G_PI);
				break;
		}
	}
	if(value < 0.0){
		float converted_value = 1.0 + value / 2.0;
		cairo_set_source_rgb(cr, 1.0, converted_value, converted_value);
	}else{
		cairo_set_source_rgb(cr, 1.0, value / 2.0, 1.0);
	}
	cairo_fill_preserve(cr);
	cairo_restore(cr);
	cairo_stroke(cr);
}

static void draw_weight(cairo_t *cr,
			guint startx,
			guint starty,
			guint endx,
			guint endy,
			float value)
{
	cairo_save(cr);
	if(value < 0.001 && value > -0.001){
		cairo_set_source_rgb(cr, 1.0, 0.0, 0.0);
	}else if(value < 0.0){
		value = 1.0 + value / 2.0;
		cairo_set_source_rgb(cr, value, 1.0, value);
	}else{
		value = value / 2.0;
		cairo_set_source_rgb(cr, value, value, value);
	}
	cairo_move_to (cr, startx, starty);
	cairo_line_to (cr, endx, endy);
	cairo_set_line_width(cr, 3.0 + value);
	cairo_stroke(cr);
	cairo_restore(cr);
}

static void draw_neat_network(cairo_t *cr,
			      GtkStyleContext *context,
			      size_t network,
			      guint x,
			      guint y,
			      guint width,
			      guint height)
{
	int radius = MIN(width, height) / 20;
	int xoffset = width / 10, yoffset = height / 20;

	if(worst == network){
		cairo_save(cr);
		cairo_rectangle(cr, x, y, width, height);
		cairo_set_source_rgb(cr, 1.0, 0.5, 0.5);
		cairo_fill(cr);
		cairo_restore(cr);
	}

	size_t species = neat_get_species_id(neat, network);

	/* Render info text */
	cairo_set_font_size(cr, 13);
	cairo_move_to(cr, x, y + 13);
	char text[256];
	snprintf(text,
		 256,
		 "Species: %d, err: %g",
		 (int)species,
		 errors[network]);
	cairo_show_text(cr, text); 
	cairo_stroke(cr);

	const struct nn_ffnet *n = neat_get_network(neat, network);

	float *neuron = n->output;
	float *weight = n->weight;
	char *activation = n->activation;

	x += radius + xoffset;
	y += radius + yoffset + 13;
	guint xinc = radius * 2 + xoffset;
	guint yinc = radius * 2 + yoffset;
	guint starty = y;

	/* Draw weights */
	size_t layer_count = n->nhiddens;
	if(n->nhidden_layers == 0){
		layer_count = n->noutputs;
	}
	for(size_t i = 0; i < layer_count; i++){
		for(size_t j = 0; j < n->ninputs + 1; j++){
			draw_weight(cr,
				    x,
				    y + yinc * j,
				    x + xinc,
				    y + yinc * (i + 1),
				    *weight++); 
		}
	}
	for(size_t i = 0; i < n->nhidden_layers; i++){
		layer_count = n->nhiddens;
		if(i == n->nhidden_layers - 1){
			layer_count = n->noutputs;
		}
		for(size_t j = 0; j < layer_count; j++){
			for(size_t k = 0; k < n->nhiddens + 1; k++){
				draw_weight(cr,
					    x + xinc * (i + 1),
					    y + yinc * k,
					    x + xinc * (i + 2),
					    y + yinc * (j + 1),
					    *weight++); 
			}
		}
	}

	/* Draw neurons */
	draw_neuron(cr, x, y, radius, n->bias, true, 0);
	y += yinc;
	for(size_t i = 0; i < n->ninputs; i++){
		draw_neuron(cr, x, y, radius, *neuron++, false, 0);
		y += yinc;
	}

	x += xinc;
	y = starty;

	for(size_t i = 0; i < n->nhidden_layers; i++){
		draw_neuron(cr, x, y, radius, n->bias, true, 0);
		y += yinc;
		for(size_t j = 0; j < n->nhiddens; j++){
			draw_neuron(cr,
				    x,
				    y,
				    radius,
				    *neuron++,
				    false,
				    *activation++);
			y += yinc;
		}
		x += xinc;
		y = starty;
	}

	for(size_t i = 0; i < n->noutputs; i++){
		y += yinc;
		draw_neuron(cr, x, y, radius, *neuron++, false, *activation++);
	}
}

static void clear()
{
	cairo_t *cr = cairo_create(surface);

	cairo_set_source_rgb(cr, 1, 1, 1);
	cairo_paint(cr);

	cairo_destroy(cr);
}

static gboolean draw(GtkWidget *widget, cairo_t *cr, gpointer data)
{
	char frame_text[256];

	cairo_set_line_width(cr, 2.0);

	GtkStyleContext *context = gtk_widget_get_style_context(widget);
	guint width = gtk_widget_get_allocated_width(widget);
	guint height = gtk_widget_get_allocated_height(widget);
	gtk_render_background(context, cr, 0, 0, width, height);

	GdkRGBA color;
	gtk_style_context_get_color(context,
				    gtk_style_context_get_state(context),
				    &color);
	gdk_cairo_set_source_rgba(cr, &color);

	cairo_set_font_size(cr, 13);
	cairo_move_to(cr, 3, 13);
	snprintf(frame_text, 256, "Frame: %d", (int)frame);
	cairo_show_text(cr, frame_text); 
	cairo_stroke(cr);

	guint xoffset = 100;
	width -= xoffset;
	for(size_t y = 0; y < rendery; y++){
		for(size_t x = 0; x < renderx; x++){
			draw_neat_network(cr,
					  context,
					  x + y * renderx,
					  x * (width / renderx) + xoffset,
					  y * (height / rendery),
					  width / renderx,
					  height / rendery);
		}
	}

	return FALSE;
}

static gboolean configure(GtkWidget *widget,
			  GdkEventConfigure *event,
			  gpointer data)
{
	if (surface){
		cairo_surface_destroy (surface);
	}

	GdkWindow *window = gtk_widget_get_window(widget);
	int width = gtk_widget_get_allocated_width(widget);
	int height = gtk_widget_get_allocated_height(widget);
	surface = gdk_window_create_similar_surface(window,
						    CAIRO_CONTENT_COLOR,
						    width,
						    height);

	clear();

	return TRUE;
}

static void close_window()
{
	g_source_remove(thread);

	if(surface){
		cairo_surface_destroy(surface);
	}
}

static gboolean change_render_x(GtkSpinButton *spin, gpointer data)
{
	GtkAdjustment *adjustment = gtk_spin_button_get_adjustment(spin);
	int new = (int)gtk_adjustment_get_value(adjustment);
	if(new * rendery <= POP_SIZE){
		renderx = new;
	}else{
		gtk_spin_button_set_value(spin, renderx);
	}

	return TRUE;
}

static gboolean change_render_y(GtkSpinButton *spin, gpointer data)
{
	GtkAdjustment *adjustment = gtk_spin_button_get_adjustment(spin);
	int new = (int)gtk_adjustment_get_value(adjustment);
	if(new * renderx <= POP_SIZE){
		rendery = new;
	}else{
		gtk_spin_button_set_value(spin, rendery);
	}

	return TRUE;
}

static void activate(GtkApplication *app, gpointer user_data)
{
	GtkWidget *window = gtk_application_window_new(app);
	gtk_window_set_title(GTK_WINDOW(window), "neat-o: drawing");
	gtk_window_set_default_size(GTK_WINDOW(window), 800, 600);
	g_signal_connect(window, "destroy", G_CALLBACK(close_window), NULL);

	gtk_container_set_border_width(GTK_CONTAINER (window), 8);

	GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
	gtk_container_add(GTK_CONTAINER(window), box);

	GtkWidget *hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
	gtk_box_pack_start(GTK_BOX(box), hbox, FALSE, FALSE, 0);

	GtkWidget *xspinner = gtk_spin_button_new_with_range(1, POP_SIZE, 1);
	g_signal_connect(xspinner,
			 "value-changed",
			 G_CALLBACK(change_render_x),
			 NULL);
	gtk_container_add(GTK_CONTAINER(hbox), xspinner);

	GtkWidget *yspinner = gtk_spin_button_new_with_range(1, POP_SIZE, 1);
	g_signal_connect(yspinner,
			 "value-changed",
			 G_CALLBACK(change_render_y),
			 NULL);
	gtk_container_add(GTK_CONTAINER(hbox), yspinner);

	GtkWidget *frame = gtk_frame_new(NULL);
	gtk_box_pack_end(GTK_BOX(box), frame, TRUE, TRUE, 0);

	GtkWidget *drawing_area = gtk_drawing_area_new();
	gtk_widget_set_size_request(drawing_area, 300, 300);
	gtk_container_add(GTK_CONTAINER(frame), drawing_area);
	g_signal_connect(drawing_area, "draw", G_CALLBACK(draw), NULL);
	g_signal_connect(drawing_area,
			 "configure-event",
			 G_CALLBACK(configure),
			 NULL);

	thread = g_timeout_add(5, tick, drawing_area);

	gtk_widget_show_all(window);

	setup_neat();
}

int main(int argc, char *argv[])
{	
	srand(time(NULL));

	GtkApplication *app = gtk_application_new("org.tversteeg.neatc",
						  G_APPLICATION_FLAGS_NONE);
	g_signal_connect(app, "activate", G_CALLBACK(activate), NULL);

	int status = g_application_run(G_APPLICATION(app), argc, argv);
	g_object_unref(app);

	return status;
}
